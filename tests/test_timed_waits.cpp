// SPDX-License-Identifier: MIT
// The quiet waits in the controls: each one wakes its window once, when it is up, and keeps the
// window idle while it runs.
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/TestSpellChecker.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/menus/MenuBar.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/navigation/Carousel.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr Size Client{400.0f, 300.0f};

PointerEvent move_to(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}

// Ticks in 10 ms steps, laying out after each as a frame would, until a tick leaves the window
// idle. Returns the time that took.
double settle(HeadlessUI& ui)
{
    double elapsed = 0.0;
    for (int i = 0; i < 200 && ui.window().wants_ticks(); ++i)
    {
        (void)ui.tick(10ms);
        elapsed += 10.0;
        ui.layout(Client);
    }
    REQUIRE_FALSE(ui.window().wants_ticks());
    return elapsed;
}

// The wait the window was asked to wake for, which must be there.
double wake(HeadlessUI& ui)
{
    const std::optional<Duration> wait = ui.window().next_wake();
    REQUIRE(wait.has_value());
    return wait->count();
}

bool draws_text(Window& window, const std::string& text)
{
    TestRenderer r;
    window.render(r);
    for (const auto& command : r.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command); run != nullptr && run->text == text)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("Carousel's buttons hide three seconds after the last move, on one wake", "[flip-view][tick][timed-wait]")
{
    HeadlessUI ui   = create_headless();
    auto       flip = std::make_unique<CarouselControl>();
    for (int i = 0; i < 3; ++i)
    {
        flip->add_item(std::make_unique<TextControl>("Item " + std::to_string(i)));
    }
    CarouselControl* view = ui.window().set_content(std::move(flip));
    ui.show(Client);

    seams(ui.window()).handle_pointer(move_to(view->bounds().center()));
    REQUIRE(view->next_button_visible());
    (void)ui.tick(16ms); // the move: a busy tick
    ui.layout(Client);
    const double settled = settle(ui);

    const double wait = wake(ui);
    REQUIRE(wait == 3000.0 - 16.0 - settled);
    (void)ui.tick(Duration{wait - 1.0});
    REQUIRE(view->next_button_visible());
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(wake(ui) == 1.0);
    (void)ui.tick(1ms);
    REQUIRE_FALSE(view->next_button_visible());
}

TEST_CASE("A tip set on an element opens 800 ms after the pointer rests, on one wake", "[tooltip][tick][timed-wait]")
{
    HeadlessUI   ui    = create_headless();
    auto         owned = std::make_unique<TextControl>("Save");
    TextControl* label = owned.get();
    label->set_tool_tip("Save (Ctrl+S)");
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(owned));
    ui.window().set_content(std::move(stack));
    ui.show(Client);
    (void)ui.tick(16ms);
    ui.layout(Client);

    seams(ui.window()).handle_pointer(move_to(label->bounds().center()));
    (void)ui.tick(16ms);
    ui.layout(Client);
    const double settled = settle(ui);

    const double wait = wake(ui);
    REQUIRE(wait == 800.0 - 16.0 - settled);
    REQUIRE_FALSE(ui.tick(Duration{wait - 1.0}));
    REQUIRE_FALSE(ui.window().wants_ticks());
    ui.layout(Client);
    REQUIRE_FALSE(draws_text(ui.window(), "Save (Ctrl+S)"));
    REQUIRE(ui.tick(1ms)); // opened: the host repaints
    ui.layout(Client);
    REQUIRE(draws_text(ui.window(), "Save (Ctrl+S)"));
}

TEST_CASE("A ToolTip opens after its delay on one wake", "[tooltip][tick][timed-wait]")
{
    HeadlessUI      ui     = create_headless();
    auto            owned  = std::make_unique<ToolTipControl>();
    ToolTipControl* tip    = owned.get();
    TextControl*    anchor = tip->set_anchor(std::make_unique<TextControl>("Save"));
    tip->set_content(std::make_unique<TextControl>("Saves the document"));
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(owned));
    ui.window().set_content(std::move(stack));
    ui.show(Client);
    (void)ui.tick(16ms);
    ui.layout(Client);

    seams(ui.window()).handle_pointer(move_to(anchor->bounds().center()));
    (void)ui.tick(16ms);
    ui.layout(Client);
    const double settled = settle(ui);

    const double wait = wake(ui);
    REQUIRE(wait == tip->delay().count() - 16.0 - settled);
    (void)ui.tick(Duration{wait - 1.0});
    REQUIRE_FALSE(tip->is_open());
    REQUIRE_FALSE(ui.window().wants_ticks());
    (void)ui.tick(1ms);
    REQUIRE(tip->is_open());
}

TEST_CASE("A submenu opens and closes 400 ms after the hover, each on one wake", "[menu][cascade][tick][timed-wait]")
{
    HeadlessUI      ui    = create_headless();
    auto            owned = std::make_unique<MenuBarControl>();
    MenuBarControl* menu  = owned.get();
    menu->set_items(
        {MenuBarItem{.text = "File",
                     .items = {MenuItem::sub_item("Send to", {MenuItem{.text = "Bluetooth"}, MenuItem{.text = "Desktop"}}),
                               MenuItem{.text = "Open"}}}});
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_child_alignment(Alignment::Leading);
    stack->add(std::move(owned));
    ui.window().set_content(std::move(stack));
    ui.show(Client);

    const Point header{menu->bounds().left() + 10.0f, menu->bounds().center().y};
    seams(ui.window()).handle_pointer({header, PointerButton::Left, PointerAction::Down});
    seams(ui.window()).handle_pointer({header, PointerButton::Left, PointerAction::Up});
    ui.layout(Client);
    (void)ui.tick(16ms);
    ui.layout(Client);
    (void)settle(ui); // the menu's open animation
    Element* root = ui::testing::popup_content(*menu);
    REQUIRE(root != nullptr);
    const Rect  popup = root->bounds();
    const float row   = (popup.height - 6.0f) / 2.0f;
    const Point send_to{popup.left() + 20.0f, popup.top() + 3.0f + row * 0.5f};
    const Point open{popup.left() + 20.0f, popup.top() + 3.0f + row * 1.5f};

    seams(ui.window()).handle_pointer(move_to(send_to));
    (void)ui.tick(16ms);
    ui.layout(Client);
    double settled = settle(ui);
    double wait    = wake(ui);
    REQUIRE(wait == 400.0 - 16.0 - settled);
    (void)ui.tick(Duration{wait - 1.0});
    REQUIRE_FALSE(ui::testing::popup_open(*root));
    REQUIRE_FALSE(ui.window().wants_ticks());
    (void)ui.tick(1ms);
    REQUIRE(ui::testing::popup_open(*root));
    ui.layout(Client);
    (void)settle(ui); // the submenu's open animation

    seams(ui.window()).handle_pointer(move_to(open));
    (void)ui.tick(16ms);
    ui.layout(Client);
    settled = settle(ui);
    wait    = wake(ui);
    REQUIRE(wait == 400.0 - 16.0 - settled);
    (void)ui.tick(Duration{wait - 1.0});
    REQUIRE(ui::testing::popup_open(*root));
    REQUIRE_FALSE(ui.window().wants_ticks());
    (void)ui.tick(1ms);
    ui.layout(Client);
    (void)settle(ui); // the submenu's close animation
    REQUIRE_FALSE(ui::testing::popup_open(*root));
}

TEST_CASE("A spell check runs 250 ms after the last keystroke, on one wake", "[textbox][spellcheck][tick][timed-wait]")
{
    HeadlessUI       ui = create_headless();
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    seams(ui.window()).set_spell_checker(&checker);
    TextBoxControl* box = ui.window().set_content(std::make_unique<TextBoxControl>());
    ui.show(Client);
    REQUIRE(ui.window().focus_element(box));
    (void)ui.tick(16ms);
    ui.layout(Client);
    (void)settle(ui);

    seams(ui.window()).handle_text("T");
    seams(ui.window()).handle_text("h");
    seams(ui.window()).handle_text("s");
    const int checks = checker.check_count();
    (void)ui.tick(16ms); // the keystrokes: a busy tick
    ui.layout(Client);
    const double settled = settle(ui);

    const double wait = wake(ui); // sooner than the caret's next blink
    REQUIRE(wait == TextBoxControl::SpellCheckDelayMs - 16.0 - settled);
    (void)ui.tick(Duration{wait - 1.0});
    REQUIRE(checker.check_count() == checks);
    REQUIRE_FALSE(ui.window().wants_ticks());
    (void)ui.tick(1ms);
    REQUIRE(checker.check_count() == checks + 1);
    REQUIRE(box->spelling_errors().size() == 1);
}

TEST_CASE("A Scroll's bar starts to fade 2 s after the last activity, on one wake",
          "[scroll-view][scrollbar][tick][timed-wait]")
{
    HeadlessUI     ui       = create_headless();
    auto           scroller = std::make_unique<ScrollControl>();
    ScrollControl* view     = scroller.get();
    TextControl*   tall     = view->set_child(std::make_unique<TextControl>("Tall"));
    tall->set_height(2000.0f);
    ui.window().set_content(std::move(scroller));
    ui.show(Client);
    REQUIRE(view->computed_vertical_scroll_bar_visible());

    (void)ui.tick(16ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
    const double wait = wake(ui);
    REQUIRE(wait >= 2000.0 - 16.0);
    REQUIRE(wait <= 2000.0 - 16.0 + 2.0); // rounded up to a whole millisecond and one more

    (void)ui.tick(Duration{wait - 2.0});
    REQUIRE_FALSE(ui.window().wants_ticks()); // not fading yet
    (void)ui.tick(Duration{wake(ui)});
    REQUIRE(ui.window().wants_ticks()); // the fade: an animation, so busy
}

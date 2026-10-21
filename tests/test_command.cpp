// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/panels/Stack.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
} // namespace

TEST_CASE("standard_command() fills the label, symbol, shortcut and description", "[command]")
{
    const Command        del    = standard_command(StandardCommand::Delete);
    const CommandValues& values = del.current();
    REQUIRE(values.text == "Delete");
    REQUIRE(values.icon.glyph() != nullptr);
    REQUIRE(values.icon.glyph()->codepoint == static_cast<char32_t>(Symbol::Delete));
    REQUIRE(values.shortcut == KeyCombo{Key::Delete});
    REQUIRE(values.description == "Delete the selected content");
    REQUIRE(values.enabled);

    const Command redo = standard_command(StandardCommand::Redo);
    REQUIRE(redo.current().shortcut.text() == "Ctrl+Y");
    const Command share = standard_command(StandardCommand::Share);
    REQUIRE(share.current().shortcut.empty());
    REQUIRE(standard_command(StandardCommand::Open).current().icon.glyph()->codepoint ==
            static_cast<char32_t>(Symbol::OpenFile));
}

TEST_CASE("A command's handler takes the parameter or not, and `enabled` gates it", "[command]")
{
    std::string    removed;
    Property<bool> allowed{false};
    Command        command{.enabled = allowed, .execute = [&](std::string_view item) { removed = item; }};
    command("List item 3");
    REQUIRE(removed.empty());
    allowed = true;
    command("List item 3");
    REQUIRE(removed == "List item 3");
}

TEST_CASE("A command's bound fields stay current, and a change is announced", "[command]")
{
    Property<std::string> label{"Save"};
    Property<KeyCombo>    shortcut{Ctrl + Key::S};
    Command               save{.text = label, .shortcut = shortcut, .description = "Save the file"};
    int                   changes = 0;
    Subscription          watch   = save.observe([&] { ++changes; });
    REQUIRE(save.current().text == "Save");

    label = "Save all";
    CHECK(save.current().text == "Save all");
    CHECK(changes == 1);
    shortcut = Ctrl + Shift + Key::S;
    CHECK(save.current().shortcut.text() == "Ctrl+Shift+S");
    CHECK(changes == 2);
    CHECK(save.current().description == "Save the file"); // a plain value stays as it is
}

TEST_CASE("A field set after construction counts until the command is first used", "[command]")
{
    Property<bool> can{false};
    Command        del = standard_command(StandardCommand::Delete);
    del.enabled        = can;
    ButtonControl button{"Delete"};
    button.set_command(del);
    REQUIRE_FALSE(button.is_enabled());
    can = true;
    REQUIRE(button.is_enabled());

    // Assigning a whole new command reads it again and tells the button.
    del = Command{.enabled = false};
    REQUIRE_FALSE(button.is_enabled());
}

TEST_CASE("A property changed between making a command and its first use shows its new value", "[command]")
{
    Property<bool>        has_selection{false};
    Property<std::string> label{"Delete"};
    Command               del{.text = label, .enabled = has_selection};

    has_selection = true; // before any control has used the command
    label         = "Delete all";
    ButtonControl button{"Delete"};
    button.set_command(del);
    CHECK(button.is_enabled());
    CHECK(del.current().text == "Delete all");

    has_selection = false; // and it keeps following from there
    CHECK_FALSE(button.is_enabled());
}

TEST_CASE("Button.Command runs after Click with the CommandParameter", "[command][button]")
{
    std::vector<std::string> order;
    Command command{.execute = [&](std::string_view p) { order.push_back(std::string("command ") + std::string(p)); }};
    ButtonControl button{"Go"};
    button.on_click([&] { order.emplace_back("click"); });
    button.set_command(command, "x");
    ui::testing::set_focused(button, true);
    button.handle_key(KeyEvent{Key::Enter, KeyAction::Down});
    button.handle_key(KeyEvent{Key::Enter, KeyAction::Up});
    REQUIRE(order == std::vector<std::string>{"click", "command x"});
}

TEST_CASE("A button fires its command's shortcut app-wide, also with nothing focused", "[command][button][shortcut]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    int                  ran = 0;
    Property<bool>       can{true};
    Property<KeyCombo>   chord{Ctrl + Key::D};
    Command              command{
        .shortcut = chord,
        .enabled  = can,
        .execute  = [&] { ++ran; },
    };
    auto           stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* button = stack->add(std::make_unique<ButtonControl>("Favorite"));
    button->set_command(command);
    ButtonControl* plain = stack->add(std::make_unique<ButtonControl>("Other"));
    window.set_content(std::move(stack));
    window.layout({400.0f, 300.0f}, measurer);
    REQUIRE(window.focused() == nullptr);

    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::D, .modifiers = {.ctrl = true}});
    CHECK(ran == 1);
    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::D}); // no Ctrl: not the chord
    CHECK(ran == 1);

    ui::testing::seams(window).handle_pointer(down(plain->bounds().center())); // focus moves to it
    ui::testing::seams(window).handle_pointer(up(plain->bounds().center()));
    REQUIRE(window.focused() == plain);
    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::D, .modifiers = {.ctrl = true}});
    CHECK(ran == 2);

    chord = Ctrl + Key::E; // the button follows a bound shortcut
    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::D, .modifiers = {.ctrl = true}});
    CHECK(ran == 2);
    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::E, .modifiers = {.ctrl = true}});
    CHECK(ran == 3);

    can = false;
    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::E, .modifiers = {.ctrl = true}});
    CHECK(ran == 3); // a disabled button does not fire
    (void)button;
}

TEST_CASE("A button inside a ToolTip keeps its command's shortcut", "[command][button][tooltip][shortcut]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    int                  ran = 0;
    Command              command{.shortcut = Ctrl + Key::D, .execute = [&] { ++ran; }};
    auto                 button = std::make_unique<ButtonControl>("Favorite");
    button->set_command(command);
    auto tip = std::make_unique<ToolTipControl>();
    tip->set_anchor(std::move(button));
    window.set_content(std::move(tip));
    window.layout({400.0f, 300.0f}, measurer);

    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::D, .modifiers = {.ctrl = true}});
    CHECK(ran == 1);
}

TEST_CASE("A shortcut on the window's content fires with nothing focused", "[shortcut]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    int                  fired = 0;
    auto                 stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add_shortcut({.keys = Ctrl + Key::N, .on_pressed = [&] { ++fired; }});
    stack->add(std::make_unique<ButtonControl>("Go"));
    window.set_content(std::move(stack));
    window.layout({400.0f, 300.0f}, measurer);
    REQUIRE(window.focused() == nullptr);

    ui::testing::seams(window).handle_key(KeyEvent{.key = Key::N, .modifiers = {.ctrl = true}});
    CHECK(fired == 1);
}

TEST_CASE("A chord is written the way the menu shows it and matches exactly its modifiers", "[command][shortcut]")
{
    constexpr KeyCombo save = Ctrl + Key::S;
    STATIC_REQUIRE(save == KeyCombo{Key::S, {.ctrl = true}});
    STATIC_REQUIRE(KeyCombo{Key::F4} == KeyCombo{Key::F4, {}});
    const KeyCombo f4 = Key::F4; // a bare key is a chord
    CHECK(f4.modifiers.none());

    CHECK(save.matches(KeyEvent{.key = Key::S, .modifiers = {.ctrl = true}}));
    CHECK_FALSE(save.matches(KeyEvent{.key = Key::S, .modifiers = {.ctrl = true, .shift = true}}));
    CHECK_FALSE(save.matches(KeyEvent{.key = Key::S, .modifiers = {.ctrl = true, .alt = true}}));
    CHECK_FALSE(save.matches(KeyEvent{.key = Key::S, .action = KeyAction::Up, .modifiers = {.ctrl = true}}));
    CHECK((Alt + Key::Enter).matches(KeyEvent{.key = Key::Enter, .modifiers = {.alt = true}}));
    CHECK((Ctrl + Alt + Key::X).matches(KeyEvent{.key = Key::X, .modifiers = {.ctrl = true, .alt = true}}));
}

TEST_CASE("An Alt chord reaches a command through the window", "[command][shortcut]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    int                  ran = 0;
    Command              fullscreen{.shortcut = Alt + Key::Enter, .execute = [&] { ++ran; }};
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl*       button = stack->add(std::make_unique<ButtonControl>("Full screen"));
    button->set_command(fullscreen);
    window.set_content(std::move(stack));
    window.layout({400.0f, 300.0f}, measurer);

    CHECK(ui::testing::seams(window).handle_key(KeyEvent{.key = Key::Enter, .modifiers = {.alt = true}}));
    CHECK(ran == 1);
    // A chord nothing uses is reported unused, so the host hands it back to the system.
    CHECK_FALSE(ui::testing::seams(window).handle_key(KeyEvent{.key = Key::Q, .modifiers = {.alt = true}}));
}

TEST_CASE("Shortcut text: Win, Ctrl, Alt, Shift, then the key name", "[command][shortcut]")
{
    REQUIRE(shortcut_text(Ctrl + Key::S) == "Ctrl+S");
    REQUIRE(shortcut_text(Ctrl + Shift + Key::N) == "Ctrl+Shift+N");
    REQUIRE(shortcut_text(Alt + Key::Enter) == "Alt+Enter");
    REQUIRE(shortcut_text(Ctrl + Key::Slash) == "Ctrl+/");
    REQUIRE(shortcut_text(Ctrl + Key::Minus) == "Ctrl+-");
    REQUIRE(shortcut_text(Shift + Alt + Ctrl + Win + Key::K) == "Win+Ctrl+Alt+Shift+K");
    REQUIRE(shortcut_text(Key::Delete) == "Delete");
    REQUIRE(shortcut_text(Ctrl + Key::Subtract) == "Ctrl+-");
    REQUIRE(shortcut_text(Ctrl + Key::Add) == "Ctrl++");
    REQUIRE(shortcut_text(Key::F4) == "F4");
    REQUIRE(shortcut_text(Ctrl + Key::Unknown).empty());
}

TEST_CASE("IconSource draws as a Uniform ScaleToFit: its natural size scaled into the box, centered", "[icon-source]")
{
    HeadlessTextMeasurer measurer;
    const IconSource     path = IconSource::path(*PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24"));
    REQUIRE(path.measure(measurer) == Size{24.0f, 24.0f}); // a path icon: the origin to the bottom-right corner
    TestRenderer r;
    path.draw(r, Rect{0, 0, 32, 16}, colors::Black);
    const auto& geometry = std::get<DrawGeometry>(r.commands.front());
    REQUIRE(geometry.brush_box == Rect{8, 0, 16, 16});

    const IconSource glyph = Symbol::Like;
    TestRenderer     g;
    glyph.draw(g, Rect{0, 0, 16, 16}, colors::Black);
    REQUIRE(std::get<DrawTextRun>(g.commands.front()).style.font_size == 16.0f); // a 20px symbol icon, scaled to 16
}

TEST_CASE("A tooltip set on an element opens after the pointer rests 800 ms, and a click closes it", "[tooltip]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 owned  = std::make_unique<ButtonControl>("Save");
    ButtonControl*       button = owned.get();
    button->set_tool_tip("Save (Ctrl+S)");
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(owned));
    window.set_content(std::move(stack));
    window.layout({400.0f, 300.0f}, measurer);

    ui::testing::seams(window).handle_pointer(move(button->bounds().center()));
    window.tick(700ms);
    window.layout({400.0f, 300.0f}, measurer);
    TestRenderer before;
    window.render(before);
    auto has_tip = [](const TestRenderer& r)
    {
        for (const auto& cmd : r.commands)
        {
            if (const auto* text = std::get_if<DrawTextRun>(&cmd); text != nullptr && text->text == "Save (Ctrl+S)")
            {
                return true;
            }
        }
        return false;
    };
    REQUIRE_FALSE(has_tip(before));

    REQUIRE(window.tick(150ms)); // opened: the host repaints
    window.layout({400.0f, 300.0f}, measurer);
    TestRenderer after;
    window.render(after);
    REQUIRE(has_tip(after));

    // A click closes it, and it stays closed while the pointer stays on what was clicked.
    int clicks = 0;
    button->on_click([&] { ++clicks; });
    ui::testing::seams(window).handle_pointer(down(button->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(button->bounds().center()));
    REQUIRE(clicks == 1); // the tip never swallows the click
    ui::testing::seams(window).handle_pointer(move(button->bounds().center()));
    window.tick(1000ms);
    window.layout({400.0f, 300.0f}, measurer);
    TestRenderer clicked;
    window.render(clicked);
    REQUIRE_FALSE(has_tip(clicked));
}

TEST_CASE("ContextRequested bubbles from the focused element to its ancestors", "[context-requested]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl*        panel  = stack.get();
    ButtonControl*       button = stack->add(std::make_unique<ButtonControl>("Row"));
    std::optional<Point> seen;
    panel->on_context_requested([&](std::optional<Point> at) { seen = at; });
    window.set_content(std::move(stack));
    window.layout({400.0f, 300.0f}, measurer);

    const Point at = button->bounds().center();
    REQUIRE(ui::testing::seams(window).handle_context_menu(at));
    REQUIRE(seen == at);
    REQUIRE(window.focused() == button); // focused first, as a right-click does
}

TEST_CASE("A ComboBox inside an open Flyout floats its list over the flyout; Escape closes the list first",
          "[popup][nested]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 owned  = std::make_unique<FlyoutControl>();
    FlyoutControl*       flyout = owned.get();
    ButtonControl*       button = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    ComboBoxControl*     list   = flyout->set_content(std::make_unique<ComboBoxControl>());
    list->set_items({"One", "Two", "Three"});
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(owned));
    window.set_content(std::move(stack));
    window.layout({600.0f, 500.0f}, measurer);

    ui::testing::seams(window).handle_pointer(down(button->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(button->bounds().center()));
    window.layout({600.0f, 500.0f}, measurer);
    REQUIRE(flyout->is_open());

    ui::testing::seams(window).handle_pointer(down(list->bounds().center())); // the ComboBox's box, inside the flyout
    ui::testing::seams(window).handle_pointer(up(list->bounds().center()));
    window.layout({600.0f, 500.0f}, measurer);
    REQUIRE(list->expanded());
    REQUIRE(ui::testing::popup_content(*list)->bounds().height > 0.0f); // placed by the nested popup pass

    ui::testing::seams(window).handle_key(KeyEvent{Key::Escape, KeyAction::Down});
    REQUIRE_FALSE(list->expanded());
    REQUIRE(flyout->is_open()); // the flyout around it stays for the next Escape
    window.layout({600.0f, 500.0f}, measurer);
    ui::testing::seams(window).handle_key(KeyEvent{Key::Escape, KeyAction::Down});
    REQUIRE_FALSE(flyout->is_open());
}

TEST_CASE("A button follows its command's enabled state, ANDed with its own", "[command][button]")
{
    Property<bool> allowed{false};
    Command        command{.enabled = allowed, .execute = [] {}};
    ButtonControl  button{"Save"};
    button.set_command(command);
    REQUIRE_FALSE(button.is_enabled());
    allowed = true;
    REQUIRE(button.is_enabled());

    // The app's own enabled state is kept apart: disabling wins over the command, and enabling
    // does not override a disabled command.
    button.set_enabled(false);
    REQUIRE_FALSE(button.is_enabled());
    allowed = false;
    button.set_enabled(true);
    REQUIRE_FALSE(button.is_enabled());
    allowed = true;
    REQUIRE(button.is_enabled());

    // Another command (or none) stops following the old one.
    Command other{};
    button.set_command(other);
    allowed = false;
    REQUIRE(button.is_enabled());
    button.set_command(nullptr);
    REQUIRE(button.is_enabled());
}

TEST_CASE("A button without a tooltip of its own shows its command's description", "[command][button]")
{
    Property<std::string> description{"Save the file"};
    Command               save{.description = description};
    ButtonControl         button{"Save"};
    button.set_command(save);
    CHECK(button.tool_tip() == "Save the file");
    description = "Save every file";
    CHECK(button.tool_tip() == "Save every file");
    button.set_tool_tip("Own tip");
    CHECK(button.tool_tip() == "Own tip");
}

TEST_CASE("A command and a button may die in either order, and a moved command starts fresh", "[command]")
{
    Property<bool> allowed{true};
    auto           command = std::make_unique<Command>(Command{.enabled = allowed});
    {
        ButtonControl button{"Go"};
        button.set_command(*command);
        REQUIRE(allowed.observer_count() == 1); // the command follows it once, for every user
        // The button follows the object it was given, not whatever is moved out of it.
        const Command moved = std::move(*command);
        REQUIRE(allowed.observer_count() == 1);
        REQUIRE(moved.current().enabled); // reads its own copy of the binding
        REQUIRE(allowed.observer_count() == 2);
    }
    REQUIRE(allowed.observer_count() == 1); // `moved` is gone; the command still follows

    int           raised = 0;
    auto          sub    = command->observe([&] { ++raised; });
    ButtonControl outlives{"Stays"};
    outlives.set_command(*command);
    allowed = false;
    REQUIRE(raised == 1);
    REQUIRE_FALSE(outlives.is_enabled());
    command.reset(); // the command dies first: the subscriptions are severed, not left dangling
    REQUIRE_FALSE(sub.alive());
    REQUIRE(allowed.observer_count() == 0);
    // The button heard of it and acts as if it had no command.
    CHECK(outlives.command() == nullptr);
    CHECK(outlives.is_enabled());
}

TEST_CASE("A command change redraws an open menu, whose items read the command as they draw", "[command][menu]")
{
    Property<bool>    allowed{true};
    Command           command{.text = "Paste", .enabled = allowed};
    MenuFlyoutControl menu;
    menu.set_items({MenuItem::from_command(command)});
    REQUIRE(menu.items()[0].is_enabled());
    ui::testing::clear_dirty(menu);
    allowed = false;
    REQUIRE_FALSE(menu.items()[0].is_enabled());
    REQUIRE(menu.is_dirty(DirtyFlags::Visual));
    menu.set_items({});
    ui::testing::clear_dirty(menu);
    allowed = true;
    REQUIRE_FALSE(menu.is_dirty(DirtyFlags::Visual)); // no longer watching
}

TEST_CASE("A command handler may be a mutable lambda that keeps its own state", "[command]")
{
    int                last = 0;
    ui::CommandHandler next = [count = 0, &last]() mutable { last = ++count; };
    next();
    next();
    CHECK(last == 2);
}

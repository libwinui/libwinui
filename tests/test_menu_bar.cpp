// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/MenuBar.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace std::chrono_literals;

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
KeyEvent key(Key k, bool ctrl = false)
{
    return KeyEvent{k, KeyAction::Down, {.ctrl = ctrl}};
}
KeyEvent key_up(Key k)
{
    return KeyEvent{k, KeyAction::Up};
}

MenuItem cmd(std::string text, std::function<void()> on_click = {})
{
    return MenuItem{.text = std::move(text), .on_click = std::move(on_click)};
}

MenuBarControl make_menu()
{
    MenuBarControl menu;
    menu.set_items({
        MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open"), cmd("Exit")}},
        MenuBarItem{.text = "Edit", .items = {cmd("Cut"), cmd("Copy")}},
    });
    HeadlessTextMeasurer measurer;
    const Size           closed = menu.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    menu.arrange({100.0f, 100.0f, closed.width, closed.height});
    return menu;
}

// "File" and "Edit" are about the same width, so the left and right quarters of the bar hit them.
Point header_point(const MenuBarControl& menu, std::size_t index)
{
    const Rect  b = menu.bounds();
    const float y = b.top() + b.height * 0.5f;
    const float x = b.left() + b.width * (index == 0 ? 0.25f : 0.75f);
    return Point{x, y};
}

const Point Outside{-500.0f, -500.0f};

// Command hover and click only resolve through Window's popup routing. layout() runs after each
// change, as a real host does, because sync_popups() places the popups.
struct MenuWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    MenuBarControl*      menu;

    explicit MenuWindow(std::vector<MenuBarItem> items)
    {
        auto owned = std::make_unique<MenuBarControl>();
        menu       = owned.get();
        menu->set_items(std::move(items));
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        stack->set_child_alignment(Alignment::Leading);
        stack->add(std::move(owned));
        window.set_content(std::move(stack));
        window.layout({600.0f, 500.0f}, measurer);
        window.focus_content();
    }

    void relayout()
    {
        window.layout({600.0f, 500.0f}, measurer);
    }

    void click(Point p)
    {
        ui::testing::seams(window).handle_pointer(down(p));
        ui::testing::seams(window).handle_pointer(up(p));
        relayout();
    }

    void press(Key k, bool ctrl = false)
    {
        ui::testing::seams(window).handle_key(key(k, ctrl));
        ui::testing::seams(window).handle_key(KeyEvent{k, KeyAction::Up, {.ctrl = ctrl}});
        relayout();
    }

    [[nodiscard]] Rect popup() const
    {
        return ui::testing::popup_content(*menu)->bounds();
    }
    [[nodiscard]] Point row_point(std::size_t row, std::size_t rows) const
    {
        const Rect  p     = popup();
        const float inner = p.height - 2.0f - 4.0f; // the frame and the menu's padding
        const float h     = inner / static_cast<float>(rows);
        return Point{p.left() + 20.0f, p.top() + 3.0f + h * (static_cast<float>(row) + 0.5f)};
    }
};
} // namespace

TEST_CASE("New menu starts with nothing open", "[menu]")
{
    MenuBarControl m = make_menu();
    REQUIRE(m.open_index() == std::nullopt);
    REQUIRE(m.active_command() == std::nullopt);
    REQUIRE(m.enabled());
}

TEST_CASE("Clicking a header opens its menu; clicking it again closes it", "[menu][command]")
{
    MenuBarControl m  = make_menu();
    const Point    h0 = header_point(m, 0);

    m.handle_pointer(down(h0));
    m.handle_pointer(up(h0));
    REQUIRE(m.open_index() == 0);

    m.handle_pointer(down(h0));
    m.handle_pointer(up(h0));
    REQUIRE(m.open_index() == std::nullopt);
}

TEST_CASE("Clicking a different header switches which menu is open", "[menu][command]")
{
    MenuBarControl m = make_menu();
    m.handle_pointer(down(header_point(m, 0)));
    m.handle_pointer(up(header_point(m, 0)));
    REQUIRE(m.open_index() == 0);

    m.handle_pointer(down(header_point(m, 1)));
    m.handle_pointer(up(header_point(m, 1)));
    REQUIRE(m.open_index() == 1);
}

TEST_CASE("With a menu open, moving onto another header opens that one", "[menu][pointer]")
{
    MenuBarControl m = make_menu();
    m.handle_pointer(down(header_point(m, 0)));
    m.handle_pointer(up(header_point(m, 0)));
    m.handle_pointer(move(header_point(m, 1)));
    REQUIRE(m.open_index() == 1);
}

TEST_CASE("The menu hangs from its header's bottom-left corner, inside the header margin", "[menu][popup]")
{
    MenuWindow w({MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open")}}});
    w.click(header_point(*w.menu, 0));
    REQUIRE(w.menu->open_index() == 0);
    const Rect bar = w.menu->bounds();
    REQUIRE_THAT(w.popup().left(), Catch::Matchers::WithinAbs(bar.left() + 4.0f, 0.01));  // the header's 4 DIP margin
    REQUIRE_THAT(w.popup().top(), Catch::Matchers::WithinAbs(bar.bottom() - 4.0f, 0.01)); // the header's own bottom
    REQUIRE(w.popup().width >= 96.0f);                                                    // the popup's minimum width
}

TEST_CASE("Clicking a command in the floating menu fires its callback and closes the menu", "[menu][command][popup]")
{
    int        fired = 0;
    MenuWindow w({MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open", [&] { ++fired; })}}});

    w.click(header_point(*w.menu, 0));
    REQUIRE(w.menu->open_index() == 0);

    w.click(w.row_point(1, 2)); // "Open"
    REQUIRE(fired == 1);
    REQUIRE(w.menu->open_index() == std::nullopt);
}

TEST_CASE("A pointer-down outside the menu and its bar light-dismisses it", "[menu][popup]")
{
    MenuWindow w({
        MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open"), cmd("Exit")}},
        MenuBarItem{.text = "Edit", .items = {cmd("Cut"), cmd("Copy")}},
    });
    w.click(header_point(*w.menu, 0));
    REQUIRE(w.menu->open_index() == 0);

    ui::testing::seams(w.window).handle_pointer(down(Outside));
    REQUIRE(w.menu->open_index() == std::nullopt);
}

TEST_CASE("Opening a menu in a StackControl does not reflow a sibling below it", "[menu][popup]")
{
    auto            menu_owned = std::make_unique<MenuBarControl>();
    MenuBarControl* menu       = menu_owned.get();
    menu->set_items({
        MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open"), cmd("Exit")}},
        MenuBarItem{.text = "Edit", .items = {cmd("Cut"), cmd("Copy")}},
    });
    auto           button_owned = std::make_unique<ButtonControl>("Below");
    ButtonControl* btn          = button_owned.get();

    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(menu_owned));
    stack->add(std::move(button_owned));

    HeadlessTextMeasurer measurer;
    Window               window;
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);
    const float button_top_before = btn->bounds().top();

    ui::testing::seams(window).handle_pointer(down(header_point(*menu, 0)));
    ui::testing::seams(window).handle_pointer(up(header_point(*menu, 0)));
    window.layout({400.0f, 400.0f}, measurer);

    REQUIRE(menu->open_index() == 0);
    REQUIRE(btn->bounds().top() == button_top_before);
}

TEST_CASE("Escape closes an open menu without firing anything", "[menu][command]")
{
    int        fired = 0;
    MenuWindow w({MenuBarItem{.text = "File", .items = {cmd("New", [&] { ++fired; })}}});
    w.click(header_point(*w.menu, 0));
    REQUIRE(w.menu->open_index() == 0);

    w.press(Key::Escape);
    REQUIRE(w.menu->open_index() == std::nullopt);
    REQUIRE(fired == 0);
}

TEST_CASE("Up/Down move through the open menu's items and wrap", "[menu][keyboard]")
{
    MenuWindow w({MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open"), cmd("Exit")}}});
    w.click(header_point(*w.menu, 0));
    // Opened by the pointer, the first item already has focus (unshown), so Down goes to the second.
    REQUIRE(w.menu->active_command() == 0);
    w.press(Key::ArrowDown);
    REQUIRE(w.menu->active_command() == 1);
    w.press(Key::ArrowDown);
    w.press(Key::ArrowDown);
    REQUIRE(w.menu->active_command() == 0); // wrapped
    w.press(Key::ArrowUp);
    REQUIRE(w.menu->active_command() == 2);
}

TEST_CASE("Separators and disabled items are skipped by the keyboard", "[menu][keyboard]")
{
    MenuItem disabled = cmd("Disabled");
    disabled.enabled  = false;
    MenuWindow w({MenuBarItem{.text = "File", .items = {cmd("New"), MenuItem::separator(), disabled, cmd("Exit")}}});
    w.click(header_point(*w.menu, 0));
    w.press(Key::ArrowDown);
    REQUIRE(w.menu->active_command() == 3);
}

TEST_CASE("Enter invokes the focused item on release and closes", "[menu][command]")
{
    int        fired = 0;
    MenuWindow w({MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open", [&] { ++fired; })}}});
    w.click(header_point(*w.menu, 0));
    w.press(Key::ArrowDown); // -> 1 ("Open")

    ui::testing::seams(w.window).handle_key(key(Key::Enter));
    REQUIRE(fired == 0); // a MenuItem invokes on key up
    ui::testing::seams(w.window).handle_key(key_up(Key::Enter));
    REQUIRE(fired == 1);
    REQUIRE(w.menu->open_index() == std::nullopt);
}

TEST_CASE("The bar's keyboard: Left/Right move between headers, Down opens with the first item focused",
          "[menu][keyboard]")
{
    MenuWindow w({
        MenuBarItem{.text = "File", .items = {cmd("New"), cmd("Open")}},
        MenuBarItem{.text = "Edit", .items = {cmd("Cut"), cmd("Copy")}},
    });
    REQUIRE(w.menu->focused());
    w.press(Key::ArrowRight);
    REQUIRE(w.menu->focused_header() == 1);
    w.press(Key::ArrowRight); // wraps
    REQUIRE(w.menu->focused_header() == 0);
    w.press(Key::ArrowDown);
    REQUIRE(w.menu->open_index() == 0);
    REQUIRE(w.menu->active_command() == 0);

    w.press(Key::ArrowRight);
    REQUIRE(w.menu->open_index() == 1);
    w.press(Key::ArrowLeft);
    REQUIRE(w.menu->open_index() == 0);
}

TEST_CASE("A submenu opens on Right and closes on Left; Escape closes only the submenu", "[menu][cascade]")
{
    int        fired = 0;
    MenuWindow w(
        {MenuBarItem{.text = "File",
                     .items = {MenuItem::sub_item("New", {cmd("Plain Text Document", [&] { ++fired; }), cmd("Rich Text Document")}),
                               cmd("Open")}}});
    w.click(header_point(*w.menu, 0));
    REQUIRE(w.menu->active_command() == 0); // "New", the sub item

    w.press(Key::ArrowRight);
    auto* root = ui::testing::popup_content(*w.menu);
    REQUIRE(ui::testing::popup_open(*root)); // the submenu, floating from the menu presenter
    const Rect sub = ui::testing::popup_content(*root)->bounds();
    // A submenu opens 4 DIPs over the owning item's right edge, level with its top.
    REQUIRE_THAT(sub.left(), Catch::Matchers::WithinAbs(w.popup().right() - 1.0f - 4.0f, 0.5));

    w.press(Key::ArrowLeft);
    w.window.tick(100ms); // the submenu's 83 ms fade
    w.relayout();
    REQUIRE_FALSE(ui::testing::popup_open(*root));
    REQUIRE(w.menu->open_index() == 0); // the menu itself stays
    REQUIRE(w.menu->active_command() == 0);

    w.press(Key::ArrowRight);
    w.press(Key::Escape);
    REQUIRE(w.menu->open_index() == 0); // Escape closed the submenu alone
    w.press(Key::Escape);
    REQUIRE(w.menu->open_index() == std::nullopt);
    REQUIRE(fired == 0);
}

TEST_CASE("Hovering a sub item opens its submenu after the show delay; clicking in it invokes",
          "[menu][cascade][pointer]")
{
    int fired = 0;
    MenuWindow w({MenuBarItem{.text = "File",
                              .items = {MenuItem::sub_item("Send to", {cmd("Bluetooth", [&] { ++fired; }), cmd("Desktop")}),
                                        cmd("Open")}}});
    w.click(header_point(*w.menu, 0));
    auto* root = ui::testing::popup_content(*w.menu);

    ui::testing::seams(w.window).handle_pointer(move(w.row_point(0, 2)));
    w.window.tick(300ms);
    w.relayout();
    REQUIRE_FALSE(ui::testing::popup_open(*root)); // not yet: 400 ms
    w.window.tick(150ms);
    w.relayout();
    REQUIRE(ui::testing::popup_open(*root));

    const Rect  sub = ui::testing::popup_content(*root)->bounds();
    const Point first{sub.left() + 20.0f, sub.top() + 3.0f + 10.0f};
    ui::testing::seams(w.window).handle_pointer(move(first));
    w.click(first);
    REQUIRE(fired == 1);
    REQUIRE(w.menu->open_index() == std::nullopt); // invoking closes the whole cascade
}

TEST_CASE("Toggle items flip and radio items check within their group", "[menu][check]")
{
    std::vector<std::pair<std::string, bool>> changes;
    auto                                      log = [&](const std::string& name)
    { return [&changes, name](bool checked) { changes.emplace_back(name, checked); }; };
    MenuWindow w({MenuBarItem{.text  = "View",
                              .items = {MenuItem::toggle("Status bar", true, log("status")),
                                        MenuItem::separator(),
                                        MenuItem::radio("Landscape", false, log("landscape")),
                                        MenuItem::radio("Portrait", true, log("portrait"))}}});

    w.click(header_point(*w.menu, 0));
    w.click(Point{w.popup().left() + 20.0f, w.popup().top() + 3.0f + 10.0f}); // the first row
    REQUIRE_FALSE(w.menu->items()[0].items[0].is_checked());
    REQUIRE(changes == std::vector<std::pair<std::string, bool>>{{"status", false}});

    changes.clear();
    w.click(header_point(*w.menu, 0));
    w.press(Key::ArrowDown);
    w.press(Key::Enter);
    REQUIRE(w.menu->items()[0].items[2].is_checked());
    REQUIRE_FALSE(w.menu->items()[0].items[3].is_checked());
    REQUIRE(changes == std::vector<std::pair<std::string, bool>>{{"portrait", false}, {"landscape", true}});

    w.click(header_point(*w.menu, 0));
    w.press(Key::ArrowDown);
    w.press(Key::Enter);
    REQUIRE(w.menu->items()[0].items[2].is_checked());
}

TEST_CASE("An item's shortcut fires app-wide, open or not, unless a text box claims the key", "[menu][shortcut]")
{
    int  saved  = 0;
    int  copied = 0;
    auto item   = [](std::string text, Key k, std::function<void()> f)
    {
        MenuItem i = cmd(std::move(text), std::move(f));
        i.shortcut = Ctrl + k;
        return i;
    };

    auto            menu_owned = std::make_unique<MenuBarControl>();
    MenuBarControl* menu       = menu_owned.get();
    menu->set_items(
        {MenuBarItem{.text  = "File",
                     .items = {item("Save", Key::S, [&] { ++saved; }), item("Copy", Key::C, [&] { ++copied; })}}});
    auto            box_owned = std::make_unique<TextBoxControl>();
    TextBoxControl* box       = box_owned.get();
    auto            stack     = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(menu_owned));
    stack->add(std::move(box_owned));

    HeadlessTextMeasurer measurer;
    Window               window;
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);
    window.focus_content();
    window.focus_next(); // the TextBox
    REQUIRE(box->focused());

    ui::testing::seams(window).handle_key(key(Key::S, true));
    REQUIRE(saved == 1);
    ui::testing::seams(window).handle_key(key(Key::C, true)); // the TextBox's own copy
    REQUIRE(copied == 0);
    REQUIRE(menu->items()[0].items[0].display_shortcut_text() == "Ctrl+S");
}

TEST_CASE("transition_opacity(): shown whole while it slides in, faded out over 83 ms", "[menu][popup][animation]")
{
    MenuBarControl m = make_menu();
    REQUIRE(m.transition_opacity() == 0.0f);
    REQUIRE_FALSE(ui::testing::popup_open(m));

    const Point h0 = header_point(m, 0);
    m.handle_pointer(down(h0));
    m.handle_pointer(up(h0));
    REQUIRE(ui::testing::popup_open(m));
    REQUIRE(m.transition_opacity() == 1.0f);

    m.handle_pointer(down(h0)); // same header again -> close()
    m.handle_pointer(up(h0));
    REQUIRE(m.open_index() == std::nullopt); // goes None the instant close() commits...
    REQUIRE(ui::testing::popup_open(m));     // ...but the popup stays through the fade
    m.tick(41.5ms);
    REQUIRE_THAT(m.transition_opacity(), Catch::Matchers::WithinAbs(0.5f, 0.01f));
    m.tick(41.5ms);
    REQUIRE(m.transition_opacity() == 0.0f);
    REQUIRE_FALSE(ui::testing::popup_open(m));
}

TEST_CASE("Losing focus force-closes an open menu", "[menu][state]")
{
    MenuBarControl m = make_menu();
    ui::testing::set_focused(m, true);
    m.handle_pointer(down(header_point(m, 0)));
    m.handle_pointer(up(header_point(m, 0)));
    REQUIRE(m.open_index() == 0);

    ui::testing::set_focused(m, false);
    REQUIRE(m.open_index() == std::nullopt);
}

TEST_CASE("Disabled menu ignores input and cannot be focused", "[menu][state]")
{
    MenuBarControl m = make_menu();
    m.set_enabled(false);
    m.handle_pointer(down(header_point(m, 0)));
    m.handle_pointer(up(header_point(m, 0)));
    REQUIRE(m.open_index() == std::nullopt);
    REQUIRE_FALSE(m.focusable());
}

TEST_CASE("A menu with no items cannot be focused", "[menu][state]")
{
    MenuBarControl m;
    REQUIRE_FALSE(m.focusable());
}

TEST_CASE("Release outside any header cancels without opening", "[menu][command]")
{
    MenuBarControl m = make_menu();
    m.handle_pointer(down(header_point(m, 0)));
    m.handle_pointer(move(Outside));
    m.handle_pointer(up(Outside));
    REQUIRE(m.open_index() == std::nullopt);
}

TEST_CASE("Under Aero the menu bar is the era's gradient strip, its items outlined on hover", "[menu][render][aero]")
{
    const Theme    aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    MenuBarControl menu;
    menu.set_items({MenuBarItem{.text = "File", .items = {cmd("New")}}, MenuBarItem{.text = "Edit", .items = {cmd("Cut")}}});
    HeadlessTextMeasurer measurer;
    const Size           size = menu.measure({Unbounded, Unbounded}, measurer, aero);
    // Padding 7,2,8,3 and no 40 DIP floor.
    const float line = measurer.measure("", aero.menu_text_style()).height;
    CHECK(size.height == line + 5.0f);
    menu.arrange({0.0f, 0.0f, size.width, size.height});

    menu.handle_pointer(move(header_point(menu, 0)));
    TestRenderer r;
    menu.render(r, aero);
    REQUIRE(std::holds_alternative<FillRoundedRectGradient>(r.commands.front()));
    bool outline = false;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command))
        {
            outline = outline || stroke->color == Color::from_argb(0x90717070);
        }
    }
    CHECK(outline);
}

TEST_CASE("Automation sees each header as a menu item that opens its menu", "[menu][automation]")
{
    int        opened = 0;
    MenuWindow w({MenuBarItem{.text  = "File",
                              .items = {cmd("Open", [&] { ++opened; }),
                                        MenuItem::separator(),
                                        MenuItem::toggle("Autosave", false),
                                        MenuItem::sub_item("Recent", {cmd("notes.md")})}},
                  MenuBarItem{.text = "Edit", .items = {cmd("Undo")}}});

    REQUIRE(describe_automation(*w.menu) ==
            "MenuBar\n"
            "  MenuItem \"File\" [ExpandCollapse=Collapsed]\n"
            "  MenuItem \"Edit\" [ExpandCollapse=Collapsed]\n");
    const std::vector<Element*> headers = automation_children(*w.menu);
    REQUIRE(automation_parent(*headers[0]) == w.menu);

    REQUIRE(ui::automation_providers(*headers[0]).expand_collapse->set_expanded_state(true));
    w.relayout();
    REQUIRE(w.menu->open_index() == 0);
    REQUIRE(describe_automation(*w.menu) ==
            "MenuBar\n"
            "  MenuItem \"File\" [ExpandCollapse=Expanded]\n"
            "    Menu\n"
            "      MenuItem \"Open\" [Invoke]\n"
            "      Separator\n"
            "      MenuItem \"Autosave\" [Toggle=Off]\n"
            "      MenuItem \"Recent\" [ExpandCollapse=Collapsed]\n"
            "  MenuItem \"Edit\" [ExpandCollapse=Collapsed]\n");

    // The menu hangs under its header, and each item sits over its row.
    Element*                    menu  = automation_children(*headers[0]).front();
    const std::vector<Element*> items = automation_children(*menu);
    REQUIRE(automation_parent(*menu) == headers[0]);
    REQUIRE(automation_parent(*items[0]) == menu);
    CHECK(items[0]->bounds().height > 0.0f);
    CHECK(w.popup().contains(items[0]->bounds().center()));

    // A submenu opens under its item.
    REQUIRE(ui::automation_providers(*items[3]).expand_collapse->set_expanded_state(true));
    w.relayout();
    const std::vector<Element*> under_recent = automation_children(*items[3]);
    REQUIRE(under_recent.size() == 1);
    REQUIRE(automation_parent(*under_recent[0]) == items[3]);
    CHECK(automation_children(*under_recent[0]).front()->automation_name() == "notes.md");
    CHECK(automation_children(*menu).size() == 4); // listed once, under its item

    // Invoking an item runs it and closes the menu.
    REQUIRE(ui::automation_providers(*items[0]).invoke->invoke());
    CHECK(opened == 1);
    CHECK(w.menu->open_index() == std::nullopt);
    CHECK(automation_children(*headers[0]).empty()); // the closed menu is gone from the tree

    // Toggling flips the check, as a click does.
    REQUIRE(ui::automation_providers(*headers[0]).expand_collapse->set_expanded_state(true));
    w.relayout();
    REQUIRE(ui::automation_providers(*items[2]).toggle->toggle_next());
    CHECK(w.menu->items()[0].items[2].is_checked());
}

TEST_CASE("Alt or F10 moves focus to the menu bar and back, Alt+letter opens a menu", "[menu][keyboard][access]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    int                  copied = 0;
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    auto*                bar    = stack->add(std::make_unique<MenuBarControl>());
    auto*                field  = stack->add(std::make_unique<TextBoxControl>());
    bar->set_items({MenuBarItem{.text = "File", .items = {cmd("New")}},
                    MenuBarItem{.text = "Edit", .items = {cmd("Copy", [&] { ++copied; })}}});
    window.set_content(std::move(stack));
    window.layout({600.0f, 400.0f}, measurer);
    (void)window.focus(*field);
    auto seams = ui::testing::seams(window);

    REQUIRE(seams.handle_menu_access(0)); // Alt alone
    CHECK(window.focused() == bar);
    CHECK(bar->focused_header() == 0);
    REQUIRE(seams.handle_menu_access(0)); // Alt again goes back
    CHECK(window.focused() == field);

    REQUIRE(seams.handle_menu_access(U'E')); // Alt+E
    window.layout({600.0f, 400.0f}, measurer);
    CHECK(window.focused() == bar);
    CHECK(bar->open_index() == 1);
    seams.handle_key({Key::Escape, KeyAction::Down}); // closes the menu
    window.tick(200ms);                               // past its fade
    window.layout({600.0f, 400.0f}, measurer);
    CHECK(bar->open_index() == std::nullopt);
    CHECK(window.focused() == bar);
    seams.handle_key({Key::Escape, KeyAction::Down}); // and goes back
    CHECK(window.focused() == field);

    // Running an item gives focus back too.
    REQUIRE(seams.handle_menu_access(U'e'));
    window.layout({600.0f, 400.0f}, measurer);
    seams.handle_key({Key::Enter, KeyAction::Down});
    seams.handle_key({Key::Enter, KeyAction::Up});
    CHECK(copied == 1);
    CHECK(window.focused() == field);

    CHECK_FALSE(seams.handle_menu_access(U'z')); // no menu starts with Z
    CHECK(window.focused() == field);
}

// SPDX-License-Identifier: MIT
#include "ui/menus/MenuInvoke.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/MenuBar.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
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
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

struct FlyoutWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    MenuFlyoutControl*   flyout = nullptr;
    ButtonControl*       anchor = nullptr;

    explicit FlyoutWindow(std::vector<MenuItem> items, Size client = {800.0f, 600.0f}) : client_(client)
    {
        auto owned = std::make_unique<MenuFlyoutControl>();
        flyout     = owned.get();
        anchor     = flyout->set_anchor(std::make_unique<ButtonControl>("Options"));
        flyout->set_items(std::move(items));
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        stack->add(std::move(owned));
        window.set_stretch_content(false); // laid out for the control at its desired size, centered
        window.set_content(std::move(stack));
        relayout();
    }

    void relayout()
    {
        window.layout(client_, measurer);
    }
    void click(Point p)
    {
        ui::testing::seams(window).handle_pointer(down(p));
        ui::testing::seams(window).handle_pointer(up(p));
        relayout();
    }
    void press(Key k)
    {
        ui::testing::seams(window).handle_key(KeyEvent{k, KeyAction::Down});
        ui::testing::seams(window).handle_key(KeyEvent{k, KeyAction::Up});
        relayout();
    }
    void open()
    {
        click(anchor->bounds().center());
    }
    [[nodiscard]] Element* presenter() const
    {
        return ui::testing::popup_content(*flyout);
    }

private:
    Size client_;
};
} // namespace

TEST_CASE("A Button anchor opens its MenuFlyout on click, and the menu takes focus", "[menu-flyout]")
{
    FlyoutWindow w({{.text = "Reset"}, MenuItem::separator(), MenuItem::toggle("Repeat", true)});
    REQUIRE_FALSE(w.flyout->is_open());
    w.open();
    REQUIRE(w.flyout->is_open());
    REQUIRE(w.window.focused() == w.presenter()); // MenuFlyout always takes focus on open

    // The default placement, Below: the menu sits under the anchor, 4 DIPs off it
    // (FlyoutMargin), centered on it.
    const Rect menu = w.presenter()->bounds();
    REQUIRE_THAT(menu.top(), WithinAbs(w.anchor->bounds().bottom() + 4.0f, 0.01));
    REQUIRE_THAT(menu.center().x, WithinAbs(w.anchor->bounds().center().x, 0.01));

    w.press(Key::Escape);
    REQUIRE_FALSE(w.flyout->is_open());
    w.relayout();
    REQUIRE(w.window.focused() == w.flyout); // focus goes back to the anchor (the flyout forwards it)
}

TEST_CASE("The presenter's columns are menu-wide: one toggle or icon shifts every item's text", "[menu-flyout][layout]")
{
    HeadlessTextMeasurer measurer;
    auto                 width_of = [&](std::vector<MenuItem> items)
    {
        MenuFlyoutControl flyout;
        flyout.set_items(std::move(items));
        flyout.show();
        return ui::testing::popup_content(flyout)->measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light()).width;
    };
    // Labels long enough that the menu is wider than its 96 DIP minimum.
    const float plain  = width_of({{.text = "Rename the file"}, {.text = "Select"}});
    const float toggle = width_of({{.text = "Rename the file"}, MenuItem::toggle("Select")});
    MenuItem    with_icon{.text = "Rename the file"};
    with_icon.icon   = Symbol::Rename;
    const float icon = width_of({with_icon, {.text = "Select"}});
    MenuItem    both = with_icon;
    const float two  = width_of({both, MenuItem::toggle("Select")});

    REQUIRE_THAT(toggle - plain, WithinAbs(28.0, 0.01)); // one placeholder column
    REQUIRE_THAT(icon - plain, WithinAbs(28.0, 0.01));
    REQUIRE_THAT(two - plain, WithinAbs(56.0, 0.01)); // two placeholder columns
}

TEST_CASE("Shortcut text gets a column as wide as the widest, 24 after the text", "[menu-flyout][layout]")
{
    HeadlessTextMeasurer measurer;
    MenuFlyoutControl    flyout;
    MenuItem             share{.text = "Share with a friend"};
    share.shortcut = Ctrl + Key::S;
    MenuItem del{.text = "Delete"};
    del.shortcut = KeyCombo{Key::Delete};
    REQUIRE(share.display_shortcut_text() == "Ctrl+S");
    REQUIRE(del.display_shortcut_text() == "Delete");

    flyout.set_items({share, del});
    flyout.show();
    const float with_shortcuts = ui::testing::popup_content(flyout)
                                     ->measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light())
                                     .width;
    flyout.set_items({{.text = "Share with a friend"}, {.text = "Delete"}});
    flyout.show();
    const float without = ui::testing::popup_content(flyout)
                              ->measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light())
                              .width;
    TextStyle   caption = Theme::light().type.caption;
    REQUIRE_THAT(with_shortcuts - without,
                 WithinAbs(24.0f + measurer.measure("Ctrl+S", caption).width, 0.01)); // the widest, "Ctrl+S"
}

TEST_CASE("Keyboard: Down/Enter invoke from the focused menu; a toggle flips before Click", "[menu-flyout][keyboard]")
{
    std::vector<std::string> log;
    MenuItem                 reset{.text = "Reset", .on_click = [&] { log.push_back("reset"); }};
    MenuItem repeat = MenuItem::toggle("Repeat", true, [&](bool on) { log.push_back(on ? "on" : "off"); });
    repeat.on_click = [&] { log.push_back("click"); };
    FlyoutWindow w({reset, MenuItem::separator(), repeat});
    w.open();
    w.press(Key::ArrowDown); // from "Reset" (focused on open) past the separator
    w.press(Key::Enter);
    REQUIRE(log == std::vector<std::string>{"off", "click"});
    REQUIRE_FALSE(w.flyout->items()[2].is_checked());
    REQUIRE_FALSE(w.flyout->is_open());
}

TEST_CASE("A submenu cascades to the right of its item, 4 DIPs over it, and flips left without room",
          "[menu-flyout][cascade]")
{
    std::vector<MenuItem> items{
        {.text = "Open"},
        MenuItem::sub_item("Send to",
                           {{.text = "Bluetooth"},
                            {.text = "Desktop (shortcut)"},
                            MenuItem::sub_item("Compressed file",
                                               {{.text = "Compress and email"}, {.text = "Compress to .7z"}})}),
    };
    FlyoutWindow w(items, Size{1400.0f, 600.0f});
    w.open();
    w.press(Key::ArrowDown); // "Send to"
    w.press(Key::ArrowRight);
    Element* root = w.presenter();
    REQUIRE(ui::testing::popup_open(*root));
    Element* sub = ui::testing::popup_content(*root);
    REQUIRE_THAT(sub->bounds().left(), WithinAbs(root->bounds().right() - 1.0f - 4.0f, 0.5));

    w.press(Key::ArrowDown);
    w.press(Key::ArrowDown);
    w.press(Key::ArrowRight);
    REQUIRE(ui::testing::popup_open(*sub));
    REQUIRE(ui::testing::popup_content(*sub)->bounds().left() > sub->bounds().left());

    // In a narrower window the third level has no room on the right and flips to its item's left,
    // 4 DIPs over it.
    FlyoutWindow narrow(items, Size{800.0f, 600.0f});
    narrow.open();
    narrow.press(Key::ArrowDown);
    narrow.press(Key::ArrowRight);
    narrow.press(Key::ArrowDown);
    narrow.press(Key::ArrowDown);
    narrow.press(Key::ArrowRight);
    Element* second = ui::testing::popup_content(*narrow.presenter());
    Element* third  = ui::testing::popup_content(*second);
    REQUIRE_THAT(third->bounds().right(), WithinAbs(second->bounds().left() + 1.0f + 4.0f, 0.5));
}

TEST_CASE("A split menu item invokes from its body and opens its submenu from its chevron", "[menu-flyout][split]")
{
    std::string  clicked;
    FlyoutWindow w(
        {MenuItem::split("Save",
                         [&] { clicked = "Save"; },
                         {{.text = "Save as .docx", .on_click = [&] { clicked = "docx"; }}, {.text = "Save as .pdf"}})});
    w.open();
    Element*    root = w.presenter();
    const Rect  menu = root->bounds();
    const float row  = menu.top() + 3.0f + 14.0f;

    // The 38-wide chevron button at the item's right end (inside the 4 DIP item margin).
    w.click(Point{menu.right() - 1.0f - 4.0f - 10.0f, row});
    REQUIRE(ui::testing::popup_open(*root));
    REQUIRE(clicked.empty());

    w.click(Point{menu.left() + 20.0f, row});
    REQUIRE(clicked == "Save");
    REQUIRE_FALSE(w.flyout->is_open());
}

TEST_CASE("show_at() puts the menu's top-left at the point, pulled back inside the window", "[menu-flyout][context]")
{
    FlyoutWindow w({{.text = "Copy"}, {.text = "Paste"}});
    w.flyout->show_at(Point{300.0f, 200.0f});
    w.relayout();
    REQUIRE(w.presenter()->bounds().left() == 300.0f);
    REQUIRE(w.presenter()->bounds().top() == 200.0f);

    w.press(Key::Escape);
    w.flyout->show_at(Point{790.0f, 590.0f});
    w.relayout();
    REQUIRE(w.presenter()->bounds().right() <= 800.0f);
    REQUIRE(w.presenter()->bounds().bottom() <= 600.0f);
}

TEST_CASE("A context request on the anchor opens the menu at the pointer", "[menu-flyout][context]")
{
    FlyoutWindow w({{.text = "Copy"}});
    w.anchor->on_context_requested(
        [&](std::optional<Point> at) { w.flyout->show_at(at.value_or(w.anchor->bounds().center())); });
    const Point at = w.anchor->bounds().center();
    REQUIRE(ui::testing::seams(w.window).handle_context_menu(at));
    w.relayout();
    REQUIRE(w.flyout->is_open());
    REQUIRE(w.presenter()->bounds().left() == at.x);
}

TEST_CASE("An item's shortcut invokes it while the menu is closed", "[menu-flyout][shortcut]")
{
    int      copied = 0;
    MenuItem copy{.text = "Copy", .on_click = [&] { ++copied; }};
    copy.shortcut = Ctrl + Key::C;
    FlyoutWindow w({copy});
    ui::testing::seams(w.window).handle_key(KeyEvent{Key::C, KeyAction::Down, {.ctrl = true}});
    REQUIRE(copied == 1);
    REQUIRE_FALSE(w.flyout->is_open());
}

TEST_CASE("A command fills an item's text, icon and shortcut, and disables it when it cannot run",
          "[menu-flyout][command]")
{
    Property<bool> can{false};
    int            deleted = 0;
    const Command  del     = standard_command(StandardCommand::Delete, [&] { ++deleted; }, can);

    const MenuItem item = MenuItem::from_command(del);
    REQUIRE(item.display_text() == "Delete");
    REQUIRE(item.display_icon().glyph() != nullptr);
    REQUIRE(item.display_shortcut_text() == "Delete");
    REQUIRE_FALSE(item.is_enabled());
    can = true;
    REQUIRE(item.is_enabled());

    std::vector<MenuItem> root{item};
    invoke_menu_item(root, root[0], {});
    REQUIRE(deleted == 1);
}

TEST_CASE("Opening slides the menu out from its edge; closing fades it over 83 ms", "[menu-flyout][animation]")
{
    FlyoutWindow w({{.text = "Copy"}, {.text = "Paste"}});
    w.open();
    REQUIRE(w.flyout->transition_opacity() == 1.0f); // no fade in: it slides
    TestRenderer r;
    w.presenter()->render(r, Theme::light());
    REQUIRE(r.count_of<PushTransform>() == 1); // mid-slide
    w.window.tick(300ms);
    TestRenderer settled;
    w.presenter()->render(settled, Theme::light());
    REQUIRE(settled.count_of<PushTransform>() == 0);

    w.flyout->hide();
    w.window.tick(41.5ms);
    REQUIRE_THAT(w.flyout->transition_opacity(), WithinAbs(0.5f, 0.01f));
    w.window.tick(50ms);
    REQUIRE_FALSE(ui::testing::popup_open(*w.flyout));
}

// Aero menus fade in over AnimateWindow's 200 ms when SPI_GETMENUFADE is on (the Windows default)
// and close at once.
TEST_CASE("Under Aero a menu fades in over 200 ms and closes at once", "[menu-flyout][animation][aero]")
{
    FlyoutWindow w({{.text = "Copy"}, {.text = "Paste"}});
    w.window.pin_theme(resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{}));
    w.relayout();
    w.open();
    REQUIRE(w.flyout->transition_opacity() == 0.0f); // fading in, not sliding
    TestRenderer r;
    w.presenter()->render(r, w.window.theme());
    REQUIRE(r.count_of<PushTransform>() == 0);
    w.window.tick(100ms);
    REQUIRE_THAT(w.flyout->transition_opacity(), WithinAbs(0.5f, 0.01f));
    w.window.tick(150ms);
    REQUIRE(w.flyout->transition_opacity() == 1.0f);

    w.flyout->hide();
    w.window.tick(1ms);
    REQUIRE_FALSE(ui::testing::popup_open(*w.flyout));
}

TEST_CASE("Under Windows 9x a menu neither slides nor fades", "[menu-flyout][animation][retro]")
{
    FlyoutWindow w({{.text = "Copy"}, {.text = "Paste"}});
    w.window.pin_theme(resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{}));
    w.relayout();
    w.open();
    REQUIRE(w.flyout->transition_opacity() == 1.0f);
    TestRenderer r;
    w.presenter()->render(r, w.window.theme());
    REQUIRE(r.count_of<PushTransform>() == 0);
}

TEST_CASE("Under Aero a menu has the era's gutter, glossy hot row, boxed check and two-line separator",
          "[menu-flyout][render][aero]")
{
    FlyoutWindow w({{.text = "Copy"}, MenuItem::separator(), MenuItem::toggle("Word wrap", true)});
    w.window.pin_theme(resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{}));
    w.relayout();
    w.open();
    w.window.tick(300ms);
    Element* menu = w.presenter();
    REQUIRE(menu != nullptr);
    const Rect           b = menu->bounds();
    HeadlessTextMeasurer m;
    const float          line = m.measure("", w.window.theme().menu_text_style()).height;
    // Border 1, padding 2, two rows padded 3 and 3, the 12 DIP separator, padding 2, border 1.
    CHECK_THAT(b.height, WithinAbs(2.0f + 4.0f + 2.0f * (line + 6.0f) + 12.0f, 0.01));

    // Hover the first row.
    ui::testing::seams(w.window).handle_pointer(
        {Point{b.center().x, b.top() + 3.0f + line * 0.5f}, PointerButton::None, PointerAction::Move});
    TestRenderer r;
    menu->render(r, w.window.theme());
    const auto filled = [&](Color color)
    {
        for (const RenderCommand& command : r.commands)
        {
            if (const auto* fill = std::get_if<FillRoundedRect>(&command); fill != nullptr && fill->color == color)
            {
                return fill->bounds;
            }
        }
        return Rect{};
    };
    // The gutter, 1 in from the border and 28 wide, its dark line right after it.
    CHECK(filled(Color::from_argb(0xFFF1F1F1)) == Rect{b.x + 2.0f, b.y + 3.0f, 28.0f, b.height - 6.0f});
    CHECK(filled(Color::from_argb(0xFFE2E3E3)).x == b.x + 30.0f);
    // The separator starts on the gutter's light line, 30 in from the border.
    CHECK(filled(Color::from_argb(0xFFE0E0E0)).x == b.x + 31.0f);
    // The checked item's box.
    CHECK(filled(Color::from_argb(0xFFE6EFF4)).width > 0.0f);
    bool hot = false;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRectGradient>(&command))
        {
            hot = hot || (!fill->brush.stops.empty() && fill->brush.stops.front().color == Color::from_argb(0x34C5EBFF));
        }
    }
    CHECK(hot);
}

// ---- a bound `checked` ----

TEST_CASE("A bound checked follows its property in every menu built from the item", "[menu-flyout][check][binding]")
{
    Property<bool> wrap{false};
    const MenuItem item = MenuItem::toggle("Word wrap", bind(wrap));

    MenuFlyoutControl flyout;
    flyout.set_items({item});
    MenuBarControl bar;
    bar.set_items({MenuBarItem{.text = "View", .items = {MenuItem::sub_item("Text", {item})}}});
    REQUIRE(wrap.observer_count() == 2);
    REQUIRE_FALSE(flyout.items()[0].is_checked());
    REQUIRE_FALSE(bar.items()[0].items[0].items[0].is_checked());

    wrap = true;
    REQUIRE(flyout.items()[0].is_checked());
    REQUIRE(bar.items()[0].items[0].items[0].is_checked());

    // The spec form, and a plain value, which stays a constant. A binding starts from the value
    // its property had at bind(), so this one is made after the change above.
    Control<MenuFlyout> built;
    auto                node = MenuFlyout{.items   = {MenuItem::toggle("Word wrap", bind(wrap)),
                                                      {.text = "Bold", .kind = MenuItemKind::Toggle, .checked = true}},
                                          .control = built}
                                   .build();
    REQUIRE(wrap.observer_count() == 3);
    REQUIRE(built->items()[0].is_checked());
    REQUIRE(built->items()[1].is_checked());
    wrap = false;
    REQUIRE_FALSE(built->items()[0].is_checked());
    REQUIRE_FALSE(flyout.items()[0].is_checked());
    REQUIRE(built->items()[1].is_checked());
}

TEST_CASE("Clicking a bound toggle writes the property before on_checked_changed runs", "[menu-flyout][check][binding]")
{
    Property<bool>    wrap{false};
    std::vector<bool> seen; // the property as the handler finds it
    FlyoutWindow      w({MenuItem::toggle("Word wrap", bind(wrap), [&](bool) { seen.push_back(wrap.get()); })});
    MenuItem          bar_wrap = MenuItem::toggle("Word wrap", bind(wrap));
    bar_wrap.shortcut          = Ctrl + Key::W;
    MenuBarControl bar; // a second menu on the same property
    bar.set_items({MenuBarItem{.text = "View", .items = {bar_wrap}}});

    w.open();
    w.press(Key::Enter); // the first item is focused on open
    REQUIRE(wrap.get());
    REQUIRE(seen == std::vector<bool>{true});
    REQUIRE(w.flyout->items()[0].is_checked());
    REQUIRE(bar.items()[0].items[0].is_checked());

    // A shortcut invokes it as a click does, and the other menu writes the same property.
    REQUIRE(bar.handle_global_shortcut(KeyEvent{Key::W, KeyAction::Down, {.ctrl = true}}));
    REQUIRE_FALSE(wrap.get());
    REQUIRE_FALSE(w.flyout->items()[0].is_checked());
    REQUIRE(seen == std::vector<bool>{true}); // a change of the property does not call the handler
}

TEST_CASE("A bound radio group keeps its properties exclusive", "[menu-flyout][check][binding]")
{
    Property<bool>                            small{true};
    Property<bool>                            large{false};
    std::vector<std::pair<std::string, bool>> changes;
    FlyoutWindow w({MenuItem::radio("Small", bind(small), [&](bool on) { changes.emplace_back("small", on); }),
                    MenuItem::radio("Large", bind(large), [&](bool on) { changes.emplace_back("large", on); })});

    w.open();
    w.press(Key::ArrowDown);
    w.press(Key::Enter);
    REQUIRE_FALSE(small.get());
    REQUIRE(large.get());
    REQUIRE(changes == std::vector<std::pair<std::string, bool>>{{"small", false}, {"large", true}});

    // Clicking the checked one leaves both alone.
    changes.clear();
    w.open();
    w.press(Key::ArrowDown);
    w.press(Key::Enter);
    REQUIRE(large.get());
    REQUIRE_FALSE(small.get());
    REQUIRE(changes.empty());

    // The properties drive the items, and the next click unchecks what they say is checked.
    large = false;
    small = true;
    REQUIRE(w.flyout->items()[0].is_checked());
    REQUIRE_FALSE(w.flyout->items()[1].is_checked());
    w.open();
    w.press(Key::ArrowDown);
    w.press(Key::Enter);
    REQUIRE(large.get());
    REQUIRE_FALSE(small.get());
}

TEST_CASE("A bound menu item is safe whichever of menu and property goes first",
          "[menu-flyout][check][binding][lifetime]")
{
    SECTION("the menu first")
    {
        Property<bool> wrap{false};
        {
            MenuFlyoutControl flyout;
            flyout.set_items({MenuItem::toggle("Word wrap", bind(wrap))});
            MenuBarControl bar;
            bar.set_items({MenuBarItem{.text = "View", .items = {MenuItem::toggle("Word wrap", bind(wrap))}}});
            REQUIRE(wrap.observer_count() == 2);
        }
        REQUIRE(wrap.observer_count() == 0);
        wrap = true; // reaches no one
    }
    SECTION("the property first")
    {
        MenuFlyoutControl flyout;
        MenuBarControl    bar;
        {
            Property<bool> wrap{true};
            flyout.set_items({MenuItem::toggle("Word wrap", bind(wrap))});
            bar.set_items({MenuBarItem{.text = "View", .items = {MenuItem::toggle("Word wrap", bind(wrap))}}});
        }
        REQUIRE(flyout.items()[0].is_checked()); // the last value stays
        flyout.set_items({});                    // drops the dead subscriptions
        bar.set_items({});
    }
    SECTION("replacing the items unsubscribes the old ones")
    {
        Property<bool>    wrap{false};
        MenuFlyoutControl flyout;
        flyout.set_items({MenuItem::toggle("Word wrap", bind(wrap))});
        REQUIRE(wrap.observer_count() == 1);
        flyout.set_items({{.text = "Reset"}});
        REQUIRE(wrap.observer_count() == 0);
    }
    SECTION("a moved menu bar keeps following")
    {
        Property<bool> wrap{false};
        MenuBarControl first;
        first.set_items({MenuBarItem{.text = "View", .items = {MenuItem::toggle("Word wrap", bind(wrap))}}});
        MenuBarControl moved{std::move(first)};
        wrap = true;
        REQUIRE(moved.items()[0].items[0].is_checked());
    }
}

TEST_CASE("Invoking an item closes the menu with Invoked", "[menu-flyout][close]")
{
    std::optional<PopupCloseReason> closing;
    std::optional<PopupCloseReason> closed;
    FlyoutWindow                    w({MenuItem{.text = "Reset", .on_click = [] {}}});
    w.flyout->on_closing([&](const PopupClosingArgs& args) { closing = args.reason; });
    w.flyout->on_closed([&](PopupCloseReason reason) { closed = reason; });
    w.open();
    w.press(Key::Enter);
    CHECK(closing == PopupCloseReason::Invoked);
    w.flyout->tick(1000ms);
    CHECK(closed == PopupCloseReason::Invoked);
}

TEST_CASE("Unbound radio items group by runs; a separator starts a new group", "[menu-flyout][check]")
{
    FlyoutWindow w({MenuItem::radio("Landscape", true),
                    MenuItem::radio("Portrait"),
                    MenuItem::separator(),
                    MenuItem::radio("Small"),
                    MenuItem::radio("Large", true)});

    w.open();
    w.press(Key::ArrowDown); // Portrait
    w.press(Key::Enter);
    const std::vector<MenuItem>& items = w.flyout->items();
    CHECK_FALSE(items[0].is_checked());
    CHECK(items[1].is_checked());
    CHECK_FALSE(items[3].is_checked());
    CHECK(items[4].is_checked()); // the other run keeps its choice
}

TEST_CASE("Radio items bound with selects() are one group wherever they sit", "[menu-flyout][check][binding]")
{
    Property<int> size{1};
    FlyoutWindow  w({MenuItem::radio("Small", selects(size, 1)),
                     MenuItem::separator(),
                     MenuItem::sub_item("More", {MenuItem::radio("Large", selects(size, 2))})});
    CHECK(w.flyout->items()[0].is_checked());

    size = 2;
    CHECK_FALSE(w.flyout->items()[0].is_checked());
    CHECK(w.flyout->items()[2].items[0].is_checked());

    w.open();
    w.press(Key::Enter); // Small
    CHECK(size.get() == 1);
    CHECK(w.flyout->items()[0].is_checked());
    CHECK_FALSE(w.flyout->items()[2].items[0].is_checked());
}

TEST_CASE("A menu item kept and built later starts from its properties' values of that moment", "[menu-flyout][binding]")
{
    Property<std::string> label{"Undo"};
    Property<bool>        can_undo{false};
    Property<bool>        wrap{false};
    std::vector<MenuItem> items{
        {.text = label, .enabled = can_undo},
        MenuItem::toggle("Word wrap", wrap),
    };

    label    = "Undo typing"; // the items wait in a vector meanwhile
    can_undo = true;
    wrap     = true;
    FlyoutWindow w(std::move(items));
    CHECK(w.flyout->items()[0].display_text() == "Undo typing");
    CHECK(w.flyout->items()[0].is_enabled());
    CHECK(w.flyout->items()[1].is_checked());

    can_undo = false;
    CHECK_FALSE(w.flyout->items()[0].is_enabled());
}

TEST_CASE("A menu item's text, enabled state and shortcut follow their properties", "[menu-flyout][binding]")
{
    Property<std::string> label{"Undo"};
    Property<bool>        can_undo{false};
    Property<KeyCombo>    chord{Ctrl + Key::Z};
    int                   undos = 0;
    FlyoutWindow          w({{.text = label, .shortcut = chord, .enabled = can_undo, .on_click = [&] { ++undos; }}});

    CHECK(w.flyout->items()[0].display_text() == "Undo");
    CHECK_FALSE(w.flyout->items()[0].is_enabled());
    CHECK_FALSE(w.flyout->handle_global_shortcut(KeyEvent{Key::Z, KeyAction::Down, {.ctrl = true}}));

    label    = "Undo typing";
    can_undo = true;
    CHECK(w.flyout->items()[0].display_text() == "Undo typing");
    REQUIRE(w.flyout->handle_global_shortcut(KeyEvent{Key::Z, KeyAction::Down, {.ctrl = true}}));
    CHECK(undos == 1);

    chord = Ctrl + Key::Y;
    CHECK(w.flyout->items()[0].display_shortcut_text() == "Ctrl+Y");
}

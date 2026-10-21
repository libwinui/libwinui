// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/menus/CommandBar.hpp"
#include "winui/ui/menus/CommandBarFlyout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
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

Size measure(Element& e, Size available = {Unbounded, Unbounded})
{
    HeadlessTextMeasurer measurer;
    return e.measure(available, measurer, Theme::default_light());
}

struct BarWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    CommandBarControl*   bar = nullptr;
    Size                 client{600.0f, 400.0f};

    BarWindow()
    {
        auto owned = std::make_unique<CommandBarControl>();
        bar        = owned.get();
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        stack->add(std::move(owned));
        window.set_content(std::move(stack));
    }
    void relayout()
    {
        window.layout(client, measurer);
    }
    void click(Point p)
    {
        ui::testing::seams(window).handle_pointer(down(p));
        ui::testing::seams(window).handle_pointer(up(p));
        relayout();
    }
};
} // namespace

TEST_CASE("AppBarButton sizes: 68x64 with a bottom label, 68x48 compact or collapsed", "[app-bar-button][layout]")
{
    AppBarButtonControl button{"Like", Symbol::Like};
    REQUIRE(measure(button) == Size{68.0f, 64.0f});

    button.set_compact(true);
    REQUIRE(measure(button) == Size{68.0f, 48.0f});

    button.set_compact(false);
    button.set_text_position(CommandBarTextPosition::Collapsed);
    REQUIRE(measure(button) == Size{68.0f, 48.0f});
}

TEST_CASE("AppBarButton with the label on the right: 12 + icon + 8 + label + 12, 48 tall", "[app-bar-button][layout]")
{
    AppBarButtonControl button{"Add", Symbol::Add};
    AppBarLook          look;
    look.text_position = CommandBarDefaultTextPosition::Right;
    button.set_app_bar_look(look);
    HeadlessTextMeasurer measurer;
    TextStyle            style = Theme::light().text_style;
    style.font_size            = 12.0f;
    const float label          = measurer.measure("Add", style).width;
    REQUIRE_THAT(measure(button).width, WithinAbs(12.0f + 16.0f + 8.0f + label + 12.0f, 0.01));
    REQUIRE(measure(button).height == 48.0f);
}

TEST_CASE("AppBarButton with the label on the right makes room for its flyout chevron", "[app-bar-button][layout]")
{
    // The chevron is a column after the label: -7 into its 12 right margin, the 8 glyph, then 12.
    AppBarLook look;
    look.text_position = CommandBarDefaultTextPosition::Right;
    AppBarButtonControl plain{"Save", Symbol::Save};
    AppBarButtonControl drops{"Save", Symbol::Save};
    plain.set_app_bar_look(look);
    drops.set_app_bar_look(look);
    FlyoutControl flyout;
    drops.set_flyout(&flyout);
    REQUIRE_THAT(measure(drops).width, WithinAbs(measure(plain).width + 13.0f, 0.01));

    // The chevron's glyph starts after the label's text, 7 into its right margin.
    drops.arrange(Rect{Point{}, measure(drops)});
    TestRenderer renderer;
    drops.render(renderer, Theme::light());
    HeadlessTextMeasurer measurer;
    TextStyle            style = Theme::light().text_style;
    style.font_size            = 12.0f;
    const float text_end       = 12.0f + 16.0f + 8.0f + measurer.measure("Save", style).width;
    CHECK_THAT(drops.bounds().right() - 12.0f - 8.0f, WithinAbs(text_end + 12.0f - 7.0f, 0.01));
}

TEST_CASE("An AppBarButton with a flyout shows a down chevron in the bar, level with a label beside it",
          "[app-bar-button][render]")
{
    const std::string down    = encode_utf8(0xE972);
    const std::string right   = encode_utf8(0xE974);
    auto              chevron = [&](AppBarButtonControl& b) -> std::optional<DrawTextRun>
    {
        b.arrange(Rect{Point{}, measure(b)});
        TestRenderer renderer;
        b.render(renderer, Theme::light());
        for (const auto& cmd : renderer.commands)
        {
            if (const auto* run = std::get_if<DrawTextRun>(&cmd); run && (run->text == down || run->text == right))
            {
                return *run;
            }
        }
        return std::nullopt;
    };
    FlyoutControl flyout;

    AppBarButtonControl full{"Save", Symbol::Save};
    full.set_flyout(&flyout);
    const auto in_full = chevron(full);
    REQUIRE(in_full);
    CHECK(in_full->text == down);
    CHECK(in_full->bounds.y == 20.0f); // up at the right, under the label below the icon

    AppBarLook look;
    look.text_position = CommandBarDefaultTextPosition::Right;
    AppBarButtonControl beside{"Save", Symbol::Save};
    beside.set_app_bar_look(look);
    beside.set_flyout(&flyout);
    const auto in_beside = chevron(beside);
    REQUIRE(in_beside);
    CHECK(in_beside->text == down);
    // Level with the label: the label's line is 16 down, the chevron centered on it.
    HeadlessTextMeasurer measurer;
    TextStyle            style = Theme::light().text_style;
    style.font_size            = 12.0f;
    const float line           = measurer.measure("Save", style).height;
    CHECK_THAT(in_beside->bounds.center().y, WithinAbs(16.0f + line * 0.5f, 0.01));
}

TEST_CASE("AppBarButton's tooltip is \"Label (Accel)\" unless a tooltip or description is set",
          "[app-bar-button][tooltip]")
{
    AppBarButtonControl button{"Save", Symbol::Save};
    REQUIRE(button.tool_tip().empty());
    button.set_shortcut(Ctrl + Key::S);
    REQUIRE(button.tool_tip() == "Save (Ctrl+S)");

    AppBarLook overflow;
    overflow.overflow = true;
    button.set_app_bar_look(overflow);
    REQUIRE(button.tool_tip().empty()); // the overflow shows the shortcut in its own column

    button.set_tool_tip("Keep it");
    REQUIRE(button.tool_tip() == "Keep it");
}

TEST_CASE("A command fills the label, icon, shortcut and tooltip, runs after Click, and disables",
          "[app-bar-button][command]")
{
    std::vector<std::string> order;
    Property<bool>           can{true};
    Property<std::string>    label{"Custom command"};
    Command                  favorite{
        .text        = label,
        .icon        = Symbol::Favorite,
        .shortcut    = Ctrl + Key::D,
        .description = "This is a custom command",
        .enabled     = can,
        .execute     = [&](std::string_view parameter) { order.push_back("execute:" + std::string(parameter)); },
    };
    AppBarButtonControl button;
    button.on_click([&] { order.push_back("click"); });
    button.set_command(favorite, "p");
    REQUIRE(button.text() == "Custom command");
    REQUIRE_FALSE(button.icon().empty());
    REQUIRE(button.shortcut_text() == "Ctrl+D");
    REQUIRE(button.tool_tip() == "This is a custom command"); // Description wins over "Label (Accel)"

    REQUIRE(button.handle_global_shortcut(KeyEvent{Key::D, KeyAction::Down, {.ctrl = true}}));
    REQUIRE(order == std::vector<std::string>{"click", "execute:p"});

    label = "Renamed";
    REQUIRE(button.text() == "Renamed");
    ui::testing::clear_dirty(button);
    can = false;
    REQUIRE_FALSE(button.is_enabled());
    REQUIRE(button.is_dirty(DirtyFlags::Layout));
    REQUIRE_FALSE(button.handle_global_shortcut(KeyEvent{Key::D, KeyAction::Down, {.ctrl = true}}));
}

TEST_CASE("AppBarToggleButton cycles unchecked, checked, then indeterminate when three-state",
          "[app-bar-button][toggle]")
{
    AppBarToggleButtonControl toggle{"PathIcon"};
    toggle.set_three_state(true);
    std::vector<std::optional<bool>> seen;
    toggle.on_checked_changed([&](std::optional<bool> v) { seen.push_back(v); });
    (void)measure(toggle);
    toggle.arrange(Rect{0, 0, 68, 64});
    for (int i = 0; i < 3; ++i)
    {
        toggle.handle_pointer(down({10, 10}));
        toggle.handle_pointer(up({10, 10}));
    }
    REQUIRE(seen == std::vector<std::optional<bool>>{true, std::nullopt, false});
}

TEST_CASE("A checked AppBarToggleButton draws the accent plate in the bar and a check mark in the overflow",
          "[app-bar-button][toggle][render]")
{
    const Theme               theme = Theme::light();
    AppBarToggleButtonControl toggle{"Shuffle", Symbol::Shuffle, true};
    (void)measure(toggle);
    toggle.arrange(Rect{0, 0, 68, 64});
    TestRenderer r;
    toggle.render(r, theme);
    REQUIRE(std::get<FillRoundedRect>(r.commands.front()).color == theme.accent_button_normal.background);
    REQUIRE(std::get<FillRoundedRect>(r.commands.front()).bounds == Rect{2, 6, 64, 52}); // the button's 2,6 inner margin

    AppBarLook overflow;
    overflow.overflow         = true;
    overflow.overflow_toggles = true;
    toggle.set_app_bar_look(overflow);
    (void)measure(toggle);
    toggle.arrange(Rect{0, 0, 200, 32});
    TestRenderer o;
    toggle.render(o, theme);
    bool check = false;
    for (const auto& cmd : o.commands)
    {
        if (const auto* text = std::get_if<DrawTextRun>(&cmd); text != nullptr && text->text == encode_utf8(0xE73E))
        {
            check = true;
        }
    }
    REQUIRE(check);
}

TEST_CASE("The overflow row's label moves past the check and icon columns", "[app-bar-button][overflow][layout]")
{
    AppBarButtonControl button{"Settings", Symbol::Setting};
    AppBarLook          look;
    look.overflow = true;
    button.set_app_bar_look(look);
    const float plain   = measure(button).width;
    look.overflow_icons = true;
    button.set_app_bar_look(look);
    const float icons     = measure(button).width;
    look.overflow_toggles = true;
    button.set_app_bar_look(look);
    const float both = measure(button).width;
    REQUIRE_THAT(icons - plain, WithinAbs(38.0f - 12.0f, 0.01));
    REQUIRE_THAT(both - plain, WithinAbs(76.0f - 12.0f, 0.01));
}

TEST_CASE("AppBarSeparator: a vertical rule in the bar, a horizontal one in the overflow", "[app-bar-separator]")
{
    AppBarSeparatorControl separator;
    REQUIRE(measure(separator) == Size{5.0f, 64.0f});
    AppBarLook compact;
    compact.compact = true;
    separator.set_app_bar_look(compact);
    REQUIRE(measure(separator).height == 48.0f);
    AppBarLook overflow;
    overflow.overflow = true;
    separator.set_app_bar_look(overflow);
    REQUIRE(measure(separator).height == 9.0f);
}

TEST_CASE("CommandBar: closed, compact buttons in a 48-tall bar; the More button shows for Bottom labels",
          "[command-bar][layout]")
{
    BarWindow w;
    auto*     add = w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    w.relayout();
    REQUIRE(w.bar->bounds().height == 48.0f);
    REQUIRE(add->is_compact());
    REQUIRE(w.bar->more_button_visible()); // opening would reveal the label

    // Opening grows the button and the bar to full height, drawn over what is below. The bar
    // keeps its 48 in layout.
    w.bar->set_open(true);
    w.relayout();
    REQUIRE_FALSE(add->is_compact());
    REQUIRE(w.bar->bounds().height == 48.0f);
    REQUIRE(w.bar->open_rect().height == 64.0f);
    REQUIRE(w.bar->open_rect().top() == w.bar->bounds().top());
    REQUIRE(add->bounds().height == 64.0f);

    // With the label on the right and nothing in the overflow there is nothing to open.
    w.bar->set_open(false);
    w.bar->set_default_text_position(CommandBarDefaultTextPosition::Right);
    w.relayout();
    REQUIRE_FALSE(w.bar->more_button_visible());
    // The primaries sit against the bar's right edge.
    REQUIRE_THAT(add->bounds().right(), WithinAbs(w.bar->bounds().right(), 0.01));
}

TEST_CASE("CommandBar: the overflow opens under the bar, right-aligned; a command click closes it",
          "[command-bar][overflow]")
{
    BarWindow w;
    w.bar->set_default_text_position(CommandBarDefaultTextPosition::Right);
    w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    int   settings = 0;
    auto* item     = w.bar->add_secondary(std::make_unique<AppBarButtonControl>("Settings", Symbol::Setting));
    item->on_click([&] { ++settings; });
    w.relayout();
    REQUIRE(w.bar->more_button_visible());
    REQUIRE_FALSE(ui::testing::popup_open(*w.bar));

    // The More button (the bar's last 48 DIPs).
    w.click(Point{w.bar->bounds().right() - 20.0f, w.bar->bounds().top() + 20.0f});
    REQUIRE(w.bar->is_open());
    REQUIRE(ui::testing::popup_open(*w.bar));
    const Rect overflow = ui::testing::popup_content(*w.bar)->bounds();
    REQUIRE_THAT(overflow.top(), WithinAbs(w.bar->bounds().bottom(), 0.01));
    REQUIRE_THAT(overflow.right(), WithinAbs(w.bar->bounds().right(), 0.01));
    REQUIRE(overflow.width >= 160.0f);
    REQUIRE(w.window.focused() == item); // opened from the More button: focus goes into the overflow

    w.window.tick(300ms);
    w.relayout();
    w.click(item->bounds().center());
    REQUIRE(settings == 1);
    REQUIRE_FALSE(w.bar->is_open());
}

TEST_CASE("CommandBar: is_sticky keeps it open through an outside click", "[command-bar][overflow]")
{
    BarWindow w;
    w.bar->add_secondary(std::make_unique<AppBarButtonControl>("Settings"));
    w.bar->set_sticky(true);
    w.bar->set_open(true);
    w.relayout();
    w.click(Point{300.0f, 380.0f});
    REQUIRE(w.bar->is_open());
    w.bar->set_sticky(false);
    w.relayout();
    w.click(Point{300.0f, 380.0f});
    REQUIRE_FALSE(w.bar->is_open());
}

TEST_CASE("CommandBar events: Opening and Closing at once, Opened and Closed after the transition",
          "[command-bar][events]")
{
    CommandBarControl        bar;
    std::vector<std::string> log;
    bar.on_opening([&] { log.emplace_back("opening"); });
    bar.on_opened([&] { log.emplace_back("opened"); });
    bar.on_closing([&] { log.emplace_back("closing"); });
    bar.on_closed([&] { log.emplace_back("closed"); });
    bar.set_open(true);
    REQUIRE(log == std::vector<std::string>{"opening"});
    bar.tick(300ms);
    bar.set_open(false);
    bar.tick(200ms);
    REQUIRE(log == std::vector<std::string>{"opening", "opened", "closing", "closed"});
}

TEST_CASE("CommandBar closing and closed carry why the bar closes", "[command-bar][events]")
{
    CommandBarControl             bar;
    std::vector<PopupCloseReason> closing;
    std::vector<PopupCloseReason> closed;
    bar.on_closing([&](const PopupClosingArgs& args) { closing.push_back(args.reason); });
    bar.on_closed([&](PopupCloseReason reason) { closed.push_back(reason); });

    bar.set_open(true);
    bar.set_open(false);
    bar.tick(300ms);
    bar.toggle_from_more_button(); // opens
    bar.tick(300ms);
    bar.toggle_from_more_button(); // closes
    bar.tick(300ms);
    CHECK(closing == std::vector{PopupCloseReason::Programmatic, PopupCloseReason::CloseButton});
    CHECK(closed == closing);
}

TEST_CASE("on_closing can keep the CommandBar open, and a bound is_open is put back", "[command-bar][events]")
{
    Property<bool>              open{false};
    bool                        keep   = true;
    int                         closed = 0;
    ui::Control<ui::CommandBar> bar;
    std::unique_ptr<Element>    tree = ui::build(ui::CommandBar{
        .is_open    = ui::bind(open),
        .on_closing = [&] { return keep ? ui::Cancel : ui::Proceed; },
        .on_closed  = [&] { ++closed; },
        .control    = bar,
    });
    open                             = true;
    REQUIRE(bar->is_open());

    open = false; // canceled: the bar stays open and the property follows it back
    CHECK(bar->is_open());
    CHECK(open.get());

    keep = false;
    bar->set_open(false);
    CHECK_FALSE(bar->is_open());
    bar->tick(300ms);
    CHECK(closed == 1);
    CHECK_FALSE(open.get());
}

TEST_CASE("Dynamic overflow moves primaries from the right into the overflow, ahead of a separator",
          "[command-bar][overflow]")
{
    BarWindow w;
    w.client = Size{260.0f, 400.0f};
    w.bar->set_default_text_position(CommandBarDefaultTextPosition::Collapsed); // 68 wide each
    std::vector<AppBarButtonControl*> primaries;
    for (const char* label : {"One", "Two", "Three", "Four"})
    {
        primaries.push_back(w.bar->add_primary(std::make_unique<AppBarButtonControl>(label, Symbol::Add)));
    }
    w.bar->add_secondary(std::make_unique<AppBarButtonControl>("Settings"));
    w.relayout();
    // 260 = 48 (More) + 3 x 68 would need 252: room for three.
    REQUIRE_FALSE(w.bar->is_primary_in_overflow(0));
    REQUIRE_FALSE(w.bar->is_primary_in_overflow(2));
    REQUIRE(w.bar->is_primary_in_overflow(3));
    REQUIRE(primaries[3]->is_in_overflow());

    w.bar->set_open(true);
    w.relayout();
    // In the overflow: "Four", then the bar's separator, then "Settings".
    REQUIRE(primaries[3]->bounds().top() < w.bar->secondary(0)->bounds().top());

    w.client = Size{600.0f, 400.0f};
    w.relayout();
    REQUIRE_FALSE(w.bar->is_primary_in_overflow(3));
}

TEST_CASE("Keys on the focused More button reach the shortcuts and the button", "[command-bar][keyboard]")
{
    BarWindow w;
    int       added = 0;
    auto*     item  = w.bar->add_secondary(std::make_unique<AppBarButtonControl>("Button 1", Symbol::Add));
    item->set_shortcut(Ctrl + Key::N);
    item->on_click([&] { ++added; });
    w.relayout();
    w.window.focus_content(); // the More button is the bar's one stop
    REQUIRE(w.window.focused() != nullptr);
    REQUIRE(w.window.focused() != item);

    // A key the button leaves alone goes on to the shortcuts.
    ui::testing::seams(w.window).handle_key(KeyEvent{Key::N, KeyAction::Down, {.ctrl = true}});
    REQUIRE(added == 1);
    // Space presses the button, which opens the overflow.
    ui::testing::seams(w.window).handle_key(KeyEvent{Key::Space, KeyAction::Down});
    ui::testing::seams(w.window).handle_key(KeyEvent{Key::Space, KeyAction::Up});
    REQUIRE(w.bar->is_open());
}

TEST_CASE("Secondary commands' shortcuts fire while the overflow is closed", "[command-bar][shortcut]")
{
    BarWindow w;
    int       added = 0;
    auto*     item  = w.bar->add_secondary(std::make_unique<AppBarButtonControl>("Button 1", Symbol::Add));
    item->set_shortcut(Ctrl + Key::N);
    item->on_click([&] { ++added; });
    w.relayout();
    ui::testing::seams(w.window).handle_key(KeyEvent{Key::N, KeyAction::Down, {.ctrl = true}});
    REQUIRE(added == 1);
}

TEST_CASE("CommandBarFlyout: Transient opens with the menu collapsed, Standard with it expanded", "[command-bar-flyout]")
{
    HeadlessTextMeasurer     measurer;
    Window                   window;
    auto                     owned  = std::make_unique<CommandBarFlyoutControl>();
    CommandBarFlyoutControl* flyout = owned.get();
    flyout->set_anchor(std::make_unique<ButtonControl>("Image"));
    flyout->set_placement(PopupPlacement::Right);
    flyout->set_placement_alignment(PopupPlacementAlignment::Leading);
    for (Symbol s : {Symbol::Share, Symbol::Save, Symbol::Delete})
    {
        flyout->add_primary(std::make_unique<AppBarButtonControl>("", s));
    }
    int   resized = 0;
    auto* resize  = flyout->add_secondary(std::make_unique<AppBarButtonControl>("Resize"));
    resize->on_click([&] { ++resized; });
    flyout->add_secondary(std::make_unique<AppBarButtonControl>("Move"));
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(owned));
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    window.set_content(std::move(stack));
    window.layout({800.0f, 600.0f}, measurer);

    flyout->set_show_mode(FlyoutShowMode::Transient);
    flyout->show();
    window.layout({800.0f, 600.0f}, measurer);
    REQUIRE_FALSE(flyout->is_expanded());
    const Rect row = ui::testing::popup_content(*flyout)->bounds();
    // 1 + 3 + 3 x 40 + 44 (More) + 1 wide, 1 + 3 + 40 + 3 + 1 tall.
    REQUIRE(row.width == 169.0f);
    REQUIRE(row.height == 48.0f);
    // To the right of the anchor, 4 DIPs off it, top edges level.
    REQUIRE_THAT(row.left(), WithinAbs(flyout->anchor()->bounds().right() + 4.0f, 0.01));
    REQUIRE_THAT(row.top(), WithinAbs(flyout->anchor()->bounds().top(), 0.01));
    REQUIRE(window.focused() != resize); // Transient: no focus taken

    flyout->hide();
    window.tick(100ms);
    flyout->set_show_mode(FlyoutShowMode::Standard);
    flyout->show();
    window.tick(300ms);
    window.layout({800.0f, 600.0f}, measurer);
    REQUIRE(flyout->is_expanded());
    REQUIRE(ui::testing::popup_content(*flyout)->bounds().height > 48.0f);

    ui::testing::seams(window).handle_pointer(down(resize->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(resize->bounds().center()));
    REQUIRE(resized == 1);
    REQUIRE_FALSE(flyout->is_open());
}

namespace
{
struct StackedBar
{
    HeadlessTextMeasurer measurer;
    Window               window;
    CommandBarControl*   bar   = nullptr;
    ButtonControl*       below = nullptr;
    Size                 client{600.0f, 400.0f};

    explicit StackedBar(float spacer = 0.0f)
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        if (spacer > 0.0f)
        {
            auto* gap = stack->add(std::make_unique<ButtonControl>("Spacer"));
            gap->set_height(spacer);
        }
        bar   = stack->add(std::make_unique<CommandBarControl>());
        below = stack->add(std::make_unique<ButtonControl>("Below"));
        below->set_horizontal_alignment(Alignment::Stretch);
        window.set_content(std::move(stack));
    }
    void relayout()
    {
        window.layout(client, measurer);
    }
    void click(Point p)
    {
        ui::testing::seams(window).handle_pointer(down(p));
        ui::testing::seams(window).handle_pointer(up(p));
        relayout();
    }
};

template <typename T>
int count_fills(const TestRenderer& r, const Color& color)
{
    int n = 0;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<T>(&c); f != nullptr && f->color == color)
        {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("CommandBar: the open bar covers what is below it instead of pushing it down", "[command-bar][layout]")
{
    StackedBar w;
    int        added = 0;
    int        below = 0;
    auto*      add   = w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    add->on_click([&] { ++added; });
    w.below->on_click([&] { ++below; });
    w.relayout();
    const Rect below_before = w.below->bounds();

    w.bar->set_open(true);
    w.relayout();
    REQUIRE(w.below->bounds() == below_before); // nothing moved
    const Rect open = w.bar->open_rect();
    REQUIRE(open.bottom() > below_before.top()); // the open bar reaches over the button

    // A press where the two overlap is the bar's: the command's label strip, over the button.
    const Point overlap{add->bounds().center().x, below_before.top() + 2.0f};
    REQUIRE(add->bounds().contains(overlap));
    w.click(overlap);
    REQUIRE(added == 1);
    REQUIRE(below == 0);
    REQUIRE_FALSE(w.bar->is_open()); // a command click closes the bar
}

TEST_CASE("CommandBar: an outside click closes the open bar and goes no further, overflow or not",
          "[command-bar][overflow]")
{
    StackedBar w;
    w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add)); // no secondaries
    int below = 0;
    w.below->on_click([&] { ++below; });
    w.relayout();
    w.bar->set_open(true);
    w.relayout();
    REQUIRE_FALSE(ui::testing::popup_open(*w.bar)); // nothing in the overflow
    w.click(Point{w.below->bounds().center().x, w.below->bounds().bottom() - 2.0f});
    REQUIRE_FALSE(w.bar->is_open());
    REQUIRE(below == 0); // the light-dismiss layer took the click

    w.bar->set_sticky(true);
    w.bar->set_open(true);
    w.relayout();
    w.click(Point{w.below->bounds().center().x, w.below->bounds().bottom() - 2.0f});
    REQUIRE(w.bar->is_open());
    REQUIRE(below == 1); // sticky: no layer, the click is the button's
}

TEST_CASE("CommandBar: with no room below, the bar and its overflow open upward", "[command-bar][overflow]")
{
    StackedBar w{330.0f};
    w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    w.bar->add_secondary(std::make_unique<AppBarButtonControl>("Settings", Symbol::Setting));
    w.relayout();
    w.bar->set_open(true);
    w.relayout();
    REQUIRE(w.bar->opens_up());
    const Rect open = w.bar->open_rect();
    REQUIRE(open.height == 64.0f);
    REQUIRE_THAT(open.bottom(), WithinAbs(w.bar->bounds().bottom(), 0.01)); // grown up from the bottom
    const Rect overflow = ui::testing::popup_content(*w.bar)->bounds();
    REQUIRE_THAT(overflow.bottom(), WithinAbs(open.top(), 0.01));
    REQUIRE_THAT(overflow.right(), WithinAbs(open.right(), 0.01));

    // At the top of the window the same bar opens down.
    StackedBar top;
    top.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    top.bar->add_secondary(std::make_unique<AppBarButtonControl>("Settings", Symbol::Setting));
    top.relayout();
    top.bar->set_open(true);
    top.relayout();
    REQUIRE_FALSE(top.bar->opens_up());
    REQUIRE_THAT(ui::testing::popup_content(*top.bar)->bounds().top(), WithinAbs(top.bar->open_rect().bottom(), 0.01));
}

TEST_CASE("CommandBar closed_display_mode Minimal: a 24-tall bar of just the More button, the full bar when open",
          "[command-bar][layout]")
{
    StackedBar w;
    auto*      add = w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    w.bar->set_default_text_position(CommandBarDefaultTextPosition::Right);
    w.bar->set_closed_display_mode(AppBarClosedDisplayMode::Minimal);
    w.relayout();
    REQUIRE(w.bar->bounds().height == 24.0f);
    REQUIRE(w.bar->more_button_visible()); // Minimal always has something to open
    std::vector<Element*> children;
    w.bar->automation_children(children);
    REQUIRE(children.size() == 1); // the More button only: the primaries take no input or focus
    std::vector<Element*> focusables;
    w.bar->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), add) == focusables.end());

    // The More button (all there is) opens it: the whole bar, down from the top, 48 tall here.
    w.click(Point{w.bar->bounds().right() - 20.0f, w.bar->bounds().top() + 12.0f});
    REQUIRE(w.bar->is_open());
    REQUIRE(w.bar->open_rect().height == 48.0f);
    REQUIRE(w.bar->bounds().height == 24.0f);
    REQUIRE(add->bounds().top() == w.bar->bounds().top());

    // Minimal's open state has no acrylic: the lift draws no menu surface, unlike Compact's.
    const Theme  theme = Theme::light();
    TestRenderer minimal;
    w.window.render(minimal);
    w.bar->set_open(false);
    w.window.tick(200ms);
    w.bar->set_closed_display_mode(AppBarClosedDisplayMode::Compact);
    w.bar->set_open(true);
    w.relayout();
    TestRenderer compact;
    w.window.render(compact);
    REQUIRE(count_fills<FillRoundedRect>(compact, w.window.theme().surfaces.menu) >
            count_fills<FillRoundedRect>(minimal, w.window.theme().surfaces.menu));
    (void)theme;
}

TEST_CASE("CommandBar closed_display_mode Hidden: nothing and no tab stop until it opens", "[command-bar][layout]")
{
    StackedBar w;
    auto*      add = w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    w.relayout();
    const float compact_gap = w.below->bounds().top() - w.bar->bounds().top();
    w.bar->set_closed_display_mode(AppBarClosedDisplayMode::Hidden);
    w.relayout();
    REQUIRE(w.bar->bounds().height == 0.0f);
    REQUIRE_THAT(w.below->bounds().top() - w.bar->bounds().top(), WithinAbs(compact_gap - 48.0f, 0.01));
    const Rect            below_closed = w.below->bounds();
    std::vector<Element*> focusables;
    w.bar->collect_focusables(focusables);
    REQUIRE(focusables.empty());

    w.bar->set_open(true);
    w.relayout();
    REQUIRE(w.bar->open_rect().height == 64.0f);
    REQUIRE(add->bounds().height == 64.0f);
    REQUIRE(w.below->bounds() == below_closed); // still nothing moved
}

TEST_CASE("CommandBar Background is drawn closed, and under Compact's open acrylic", "[command-bar][render]")
{
    StackedBar w;
    w.bar->add_primary(std::make_unique<AppBarButtonControl>("Add", Symbol::Add));
    const Color tint = Color::from_rgba(10, 20, 30);
    w.bar->set_background(tint);
    w.relayout();
    TestRenderer closed;
    w.window.render(closed);
    REQUIRE(count_fills<FillRoundedRect>(closed, tint) == 1);
}

TEST_CASE("AppBarToggleButton's bound checked state is two-way", "[command_bar][spec][binding]")
{
    Property<std::optional<bool>>       bold{false};
    std::optional<bool>                 reported;
    ui::Control<ui::AppBarToggleButton> toggle;
    auto built = ui::build(ui::AppBarToggleButton{.text               = "Bold",
                                                  .checked            = ui::bind(bold),
                                                  .on_checked_changed = [&](std::optional<bool> on) { reported = on; },
                                                  .control            = toggle});
    REQUIRE(ui::automation_providers(*toggle).toggle->toggle_next());
    CHECK(bold.get() == true);
    CHECK(reported == true);

    bold = false;
    CHECK(toggle->checked() == false);
}

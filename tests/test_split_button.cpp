// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/DropdownButton.hpp"
#include "winui/ui/input/SplitButton.hpp"
#include "winui/ui/text/Text.hpp"
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
void click(Window& window, Point p)
{
    ui::testing::seams(window).handle_pointer(move(p));
    ui::testing::seams(window).handle_pointer(down(p));
    ui::testing::seams(window).handle_pointer(up(p));
}

// The color of the first solid-filled geometry drawn, the chevron's.
std::optional<Color> geometry_color(const TestRenderer& r)
{
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* g = std::get_if<DrawGeometry>(&c); g != nullptr && g->fill)
        {
            if (const auto* color = std::get_if<Color>(&*g->fill))
            {
                return *color;
            }
        }
    }
    return std::nullopt;
}

// A window with a Flyout hanging from `T` (a DropdownButton or a SplitButton).
template <typename T>
struct AnchorWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    FlyoutControl*       flyout;
    T*                   button;

    explicit AnchorWindow(std::unique_ptr<T> anchor)
    {
        auto owned = std::make_unique<FlyoutControl>();
        flyout     = owned.get();
        button     = flyout->set_anchor(std::move(anchor));
        (void)flyout->set_content(std::make_unique<TextControl>("Flyout content"));
        owned->set_horizontal_alignment(Alignment::Leading);
        owned->set_vertical_alignment(Alignment::Leading);
        owned->set_margin(Thickness{20.0f});
        window.set_content(std::move(owned));
        relayout();
    }
    void relayout()
    {
        window.layout({600.0f, 400.0f}, measurer);
    }
};
} // namespace

// ---- DropdownButton ----

TEST_CASE("DropdownButton keeps a 20-wide chevron column (8 margin, 12 icon) inside the padding", "[dropdown-button]")
{
    HeadlessTextMeasurer  measurer;
    const Theme&          theme = Theme::light();
    ButtonControl         plain{"Email"};
    DropdownButtonControl drop{"Email"};
    const Size            a = plain.measure({Unbounded, Unbounded}, measurer, theme);
    const Size            b = drop.measure({Unbounded, Unbounded}, measurer, theme);
    REQUIRE_THAT(b.width, WithinAbs(a.width + 20.0f, 0.01));
    REQUIRE_THAT(b.height, WithinAbs(a.height, 0.01));

    drop.arrange(Rect{10.0f, 10.0f, b.width, b.height});
    const Rect face    = drop.bounds(); // inside the focus ring's room
    const Rect chevron = drop.chevron()->bounds();
    REQUIRE_THAT(chevron.width, WithinAbs(12.0, 0.01));
    REQUIRE_THAT(chevron.height, WithinAbs(12.0, 0.01));
    REQUIRE_THAT(chevron.x + chevron.width, WithinAbs(face.x + face.width - theme.padding.right, 0.01));

    TestRenderer r;
    drop.render(r, theme);
    bool saw_label = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "Email")
        {
            saw_label = true;
            REQUIRE_THAT(t->bounds.width, WithinAbs(face.width - 20.0f, 0.01));
        }
    }
    REQUIRE(saw_label);
}

TEST_CASE("DropdownButton's chevron is the secondary text color at rest, tertiary under the pointer",
          "[dropdown-button]")
{
    HeadlessTextMeasurer  measurer;
    const Theme&          theme = Theme::light();
    DropdownButtonControl drop{"Email"};
    const Size            size = drop.measure({Unbounded, Unbounded}, measurer, theme);
    drop.arrange(Rect{0.0f, 0.0f, size.width, size.height});

    TestRenderer rest;
    drop.render(rest, theme);
    REQUIRE(geometry_color(rest) == theme.palette.text_fill_secondary);
    REQUIRE(drop.chevron()->state() == "Normal");

    drop.handle_pointer(move({5.0f, 5.0f}));
    TestRenderer hover;
    drop.render(hover, theme);
    REQUIRE(geometry_color(hover) == theme.palette.text_fill_tertiary);
    REQUIRE(drop.chevron()->state() == "PointerOver");

    drop.handle_pointer(down({5.0f, 5.0f}));
    REQUIRE(drop.chevron()->state() == "Pressed");
    drop.handle_pointer(up({5.0f, 5.0f}));
    REQUIRE(drop.chevron()->state() == "PointerOver");

    drop.set_enabled(false);
    drop.tick(16ms);
    REQUIRE(drop.chevron()->state() == "Normal");
}

TEST_CASE("DropdownButton opens its flyout on a click and reports ExpandCollapse", "[dropdown-button][flyout]")
{
    AnchorWindow<DropdownButtonControl> w{std::make_unique<DropdownButtonControl>("Email")};
    int                                 clicks = 0;
    w.button->on_click([&] { ++clicks; });
    REQUIRE(ui::automation_providers(*w.button).expand_collapse != nullptr);
    REQUIRE_FALSE(w.button->is_flyout_open());

    click(w.window, w.button->bounds().center());
    REQUIRE(clicks == 1);
    REQUIRE(w.button->is_flyout_open());
    REQUIRE(ui::automation_providers(*w.button).expand_collapse->expanded_state());

    REQUIRE(ui::automation_providers(*w.button).expand_collapse->set_expanded_state(false));
    REQUIRE_FALSE(w.button->is_flyout_open());
    REQUIRE(ui::automation_providers(*w.button).expand_collapse->set_expanded_state(true));
    REQUIRE(w.button->is_flyout_open());
}

// ---- SplitButton ----

TEST_CASE("SplitButton lays out a primary part, a 1px divider and a 35-wide secondary part", "[split-button]")
{
    HeadlessTextMeasurer measurer;
    const Theme&         theme = Theme::light();
    SplitButtonControl   split{"Choose color"};
    const Size           size = split.measure({Unbounded, Unbounded}, measurer, theme);
    const Size           text = measurer.measure("Choose color", theme.text_style);
    REQUIRE_THAT(size.width, WithinAbs(text.width + theme.padding.horizontal() + 1.0f + 35.0f, 0.01));
    split.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    const Rect face = split.bounds();
    REQUIRE_THAT(split.secondary_rect().width, WithinAbs(35.0, 0.01));
    REQUIRE_THAT(split.primary_rect().width + 1.0f + 35.0f, WithinAbs(face.width, 0.01));
    // The chevron sits 12 in from the secondary part's right edge.
    REQUIRE_THAT(split.chevron()->bounds().x + 12.0f, WithinAbs(face.x + face.width - 12.0f, 0.01));

    // An empty primary part is still 35 wide (that is its minimum width).
    SplitButtonControl empty;
    empty.set_padding(Thickness{0.0f});
    REQUIRE_THAT(empty.measure({Unbounded, Unbounded}, measurer, theme).width, WithinAbs(35.0f + 1.0f + 35.0f, 0.01));
}

TEST_CASE("SplitButton: the primary part clicks, the secondary part opens the flyout below-left",
          "[split-button][flyout]")
{
    AnchorWindow<SplitButtonControl> w{std::make_unique<SplitButtonControl>("Choose color")};
    w.flyout->set_placement(PopupPlacement::Above);
    std::vector<std::string> order;
    Command command{.execute = [&](std::string_view p) { order.push_back("command " + std::string(p)); }};
    w.button->set_command(command, "red");
    w.button->on_click([&] { order.emplace_back("click"); });

    click(w.window, w.button->primary_rect().center());
    REQUIRE(order == std::vector<std::string>{"click", "command red"});
    REQUIRE_FALSE(w.button->is_flyout_open());

    click(w.window, w.button->secondary_rect().center());
    REQUIRE(w.button->is_flyout_open());
    REQUIRE(order.size() == 2); // opening is not a click
    // Below, leading edges aligned, whatever the flyout said.
    REQUIRE(w.flyout->placement() == PopupPlacement::Below);
    REQUIRE(w.flyout->placement_alignment() == PopupPlacementAlignment::Leading);
    w.relayout();
    const Rect popup = ui::testing::popup_content(*w.flyout)->bounds();
    REQUIRE(popup.y >= w.button->bounds().y + w.button->bounds().height);
    REQUIRE_THAT(popup.x, WithinAbs(w.button->bounds().x, 1.0));
    REQUIRE(ui::testing::visual_state(*w.button) == SplitButtonState::FlyoutOpen);

    // A press on the button while its flyout is open closes it, and clicks nothing.
    click(w.window, w.button->primary_rect().center());
    REQUIRE_FALSE(w.button->is_flyout_open());
    REQUIRE(order.size() == 2);
}

TEST_CASE("SplitButton's visual state follows the part under the pointer", "[split-button]")
{
    HeadlessTextMeasurer measurer;
    SplitButtonControl   split{"Go"};
    const Size           size = split.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    split.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    const Point primary   = split.primary_rect().center();
    const Point secondary = split.secondary_rect().center();

    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::Normal);
    split.handle_pointer(move(primary));
    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::PrimaryPointerOver);
    split.handle_pointer(down(primary));
    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::PrimaryPressed);
    split.handle_pointer(up(primary));
    split.handle_pointer(move(secondary));
    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::SecondaryPointerOver);
    REQUIRE(split.chevron()->state() == "PointerOver");
    split.handle_pointer(down(secondary));
    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::SecondaryPressed);
    REQUIRE(split.chevron()->state() == "Pressed");
    split.handle_pointer({{}, PointerButton::None, PointerAction::Leave});
    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::Normal);
    split.set_enabled(false);
    REQUIRE(ui::testing::visual_state(split) == SplitButtonState::Disabled);
}

TEST_CASE("SplitButton keys: Space or Enter click on key up, Alt+Down and F4 open the flyout", "[split-button][flyout]")
{
    AnchorWindow<SplitButtonControl> w{std::make_unique<SplitButtonControl>("Go")};
    int                              clicks = 0;
    w.button->on_click([&] { ++clicks; });
    ui::testing::set_focused(*w.button, true);

    w.button->handle_key({Key::Space, KeyAction::Down});
    REQUIRE(ui::testing::visual_state(*w.button) == SplitButtonState::TouchPressed);
    REQUIRE(clicks == 0);
    w.button->handle_key({Key::Space, KeyAction::Up});
    REQUIRE(clicks == 1);
    w.button->handle_key({Key::Enter, KeyAction::Down});
    w.button->handle_key({Key::Enter, KeyAction::Up});
    REQUIRE(clicks == 2);

    w.button->handle_key({Key::ArrowDown, KeyAction::Up}); // no Alt: nothing
    REQUIRE_FALSE(w.button->is_flyout_open());
    w.button->handle_key({Key::ArrowDown, KeyAction::Down, {.alt = true}});
    REQUIRE_FALSE(w.button->is_flyout_open()); // on key up
    w.button->handle_key({Key::ArrowDown, KeyAction::Up, {.alt = true}});
    REQUIRE(w.button->is_flyout_open());
    w.button->close_flyout();
    w.button->handle_key({Key::F4, KeyAction::Up});
    REQUIRE(w.button->is_flyout_open());
    REQUIRE(clicks == 2);
}

TEST_CASE("SplitButton's command disables the primary part only, and follows its can-execute changes",
          "[split-button][command]")
{
    AnchorWindow<SplitButtonControl> w{std::make_unique<SplitButtonControl>("Go")};
    Property<bool>                   allowed{false};
    int                              ran    = 0;
    int                              clicks = 0;
    Command                          command{.enabled = allowed, .execute = [&] { ++ran; }};
    w.button->set_command(command);
    w.button->on_click([&] { ++clicks; });
    REQUIRE_FALSE(w.button->primary_enabled());
    REQUIRE(w.button->enabled());

    click(w.window, w.button->primary_rect().center());
    REQUIRE(clicks == 0);
    click(w.window, w.button->secondary_rect().center());
    REQUIRE(w.button->is_flyout_open()); // the secondary part does not care
    w.button->close_flyout();

    allowed = true;
    REQUIRE(w.button->primary_enabled());
    click(w.window, w.button->primary_rect().center());
    REQUIRE(clicks == 1);
    REQUIRE(ran == 1);
}

TEST_CASE("SplitButton draws each part clipped to its columns, with the divider between", "[split-button][render]")
{
    HeadlessTextMeasurer measurer;
    const Theme&         theme = Theme::light();
    SplitButtonControl   split{"Go"};
    const Size           size = split.measure({Unbounded, Unbounded}, measurer, theme);
    split.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    TestRenderer r;
    split.render(r, theme);
    REQUIRE(r.count_of<PushClip>() >= 2);
    bool saw_divider = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c);
            f != nullptr && f->bounds.width == 1.0f && f->color == theme.palette.control_stroke_default)
        {
            saw_divider = true;
            REQUIRE_THAT(f->bounds.x, WithinAbs(split.primary_rect().x + split.primary_rect().width, 0.01));
        }
    }
    REQUIRE(saw_divider);
    REQUIRE(geometry_color(r) == theme.palette.text_fill_secondary);
}

// ---- ToggleSplitButton ----

TEST_CASE("ToggleSplitButton toggles on the primary part only, before Click", "[split-button][toggle]")
{
    AnchorWindow<ToggleSplitButtonControl> w{std::make_unique<ToggleSplitButtonControl>("Bullets")};
    std::vector<std::string>               order;
    w.button->on_checked_changed([&](bool checked) { order.push_back(checked ? "checked" : "unchecked"); });
    w.button->on_click([&] { order.emplace_back("click"); });

    click(w.window, w.button->primary_rect().center());
    REQUIRE(w.button->checked());
    REQUIRE(order == std::vector<std::string>{"checked", "click"});

    click(w.window, w.button->secondary_rect().center());
    REQUIRE(w.button->checked());
    REQUIRE(w.button->is_flyout_open());
    w.button->close_flyout();

    REQUIRE(ui::automation_providers(*w.button).toggle != nullptr);
    REQUIRE(ui::automation_providers(*w.button).toggle->toggle_value() == ToggleState::On);
    REQUIRE(ui::automation_providers(*w.button).toggle->toggle_next());
    REQUIRE_FALSE(w.button->checked());

    w.button->set_checked(true);
    REQUIRE(order.back() == "checked");
}

TEST_CASE("A checked ToggleSplitButton draws in the accent colors with the on-accent divider",
          "[split-button][toggle][render]")
{
    HeadlessTextMeasurer     measurer;
    const Theme&             theme = Theme::light();
    ToggleSplitButtonControl split{"Go"};
    split.set_checked(true);
    const Size size = split.measure({Unbounded, Unbounded}, measurer, theme);
    split.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    TestRenderer r;
    split.render(r, theme);
    bool saw_divider = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f != nullptr && f->bounds.width == 1.0f)
        {
            saw_divider = f->color == theme.palette.control_stroke_on_accent_tertiary;
        }
    }
    REQUIRE(saw_divider);
    REQUIRE(geometry_color(r) == theme.accent_button_normal.text);
}

TEST_CASE("Under high contrast the drop-down chevrons take the button's text color", "[dropdown-button][split-button]")
{
    ThemeRequest request;
    request.mode               = ThemeMode::HighContrast;
    const Theme          theme = resolve_theme(request, OsThemeSnapshot{});
    HeadlessTextMeasurer measurer;

    DropdownButtonControl drop{"Email"};
    Size                  size = drop.measure({Unbounded, Unbounded}, measurer, theme);
    drop.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    TestRenderer drop_out;
    drop.render(drop_out, theme);
    CHECK(geometry_color(drop_out) == theme.button_normal.text);

    SplitButtonControl split{"Go"};
    size = split.measure({Unbounded, Unbounded}, measurer, theme);
    split.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    TestRenderer split_out;
    split.render(split_out, theme);
    CHECK(geometry_color(split_out) == theme.button_normal.text);

    // Nothing reads the Fluent palette here, whose colors are all opaque black outside Fluent: the
    // divider takes the text color too.
    const Color unset{};
    for (const RenderCommand& c : split_out.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&c))
        {
            CHECK_FALSE(fill->color == unset);
        }
    }
}

namespace
{
// The focus ring's outer stroke, the only 2-wide rounded rectangle a button draws.
bool draws_focus_ring(const Window& window)
{
    TestRenderer r;
    window.render(r);
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&c); stroke != nullptr && stroke->stroke_width == 2.0f)
        {
            return true;
        }
    }
    return false;
}

template <typename T>
void check_click_hides_ring(std::unique_ptr<T> anchor)
{
    AnchorWindow<T> w{std::move(anchor)};
    // A key before the first click, like the Enter that started the app.
    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Up});
    const Rect b = w.button->bounds();
    click(w.window, {b.x + 8.0f, b.y + b.height / 2.0f});
    w.relayout();
    CHECK_FALSE(draws_focus_ring(w.window));

    // Any key shows it again.
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    w.relayout();
    CHECK(draws_focus_ring(w.window));
}
} // namespace

TEST_CASE("A click on a button inside a Flyout leaves no focus ring", "[dropdown-button][split-button][focus]")
{
    SECTION("DropdownButton")
    {
        check_click_hides_ring(std::make_unique<DropdownButtonControl>("Email"));
    }
    SECTION("SplitButton")
    {
        check_click_hides_ring(std::make_unique<SplitButtonControl>("Color"));
    }
}

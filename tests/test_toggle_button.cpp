// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ToggleButton.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

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
void click(ToggleButtonControl& b)
{
    b.handle_pointer(down({110, 110}));
    b.handle_pointer(up({110, 110}));
}
ToggleButtonControl make()
{
    ToggleButtonControl b{"ToggleButton"};
    b.arrange({100, 100, 120, 32});
    return b;
}
Color background(const ToggleButtonControl& b, const Theme& theme)
{
    TestRenderer renderer;
    b.render(renderer, theme);
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&c))
        {
            return fill->color;
        }
    }
    return colors::Transparent;
}
} // namespace

TEST_CASE("A click checks and a second one clears", "[toggle-button]")
{
    ToggleButtonControl b = make();
    REQUIRE(b.checked() == false);
    click(b);
    REQUIRE(b.checked() == true);
    click(b);
    REQUIRE(b.checked() == false);
}

TEST_CASE("Three-state goes checked, indeterminate, unchecked", "[toggle-button]")
{
    ToggleButtonControl b = make();
    b.set_three_state(true);
    click(b);
    REQUIRE(b.checked() == true);
    click(b);
    REQUIRE(b.checked() == std::nullopt);
    click(b);
    REQUIRE(b.checked() == false);

    // Without three-state an indeterminate value set in code clicks to checked.
    b.set_three_state(false);
    b.set_checked(std::nullopt);
    click(b);
    REQUIRE(b.checked() == true);
}

TEST_CASE("The checked change comes before the click, and Space toggles too", "[toggle-button]")
{
    ToggleButtonControl      b = make();
    std::vector<std::string> events;
    b.on_checked_changed([&](std::optional<bool> v) { events.push_back(v == true ? "checked" : "unchecked"); });
    b.on_click([&] { events.push_back("click"); });
    click(b);
    REQUIRE(events == std::vector<std::string>{"checked", "click"});

    ui::testing::set_focused(b, true);
    b.handle_key({Key::Space, KeyAction::Down});
    b.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(b.checked() == false);
}

TEST_CASE("A checked toggle draws the accent button; unchecked the standard one", "[toggle-button][render]")
{
    const Theme         theme = Theme::light();
    ToggleButtonControl b     = make();
    REQUIRE(background(b, theme) == theme.button_normal.background);
    b.set_checked(true);
    REQUIRE(background(b, theme) == theme.accent_button_normal.background);
    b.set_checked(std::nullopt); // indeterminate looks unchecked
    REQUIRE(background(b, theme) == theme.button_normal.background);
}

TEST_CASE("Where the accent button is the standard one, checked draws pressed", "[toggle-button][render]")
{
    Theme theme                = Theme::light();
    theme.accent_button_normal = theme.button_normal;
    theme.accent_button_hover  = theme.button_hover;
    ToggleButtonControl b      = make();
    b.set_checked(true);
    REQUIRE(background(b, theme) == theme.button_pressed.background);
}

TEST_CASE("UI Automation toggles without clicking", "[toggle-button][automation]")
{
    ToggleButtonControl b      = make();
    int                 clicks = 0;
    b.on_click([&] { ++clicks; });
    REQUIRE(ui::automation_providers(b).toggle != nullptr);
    REQUIRE(ui::automation_providers(b).invoke == nullptr);
    REQUIRE(b.automation_role() == AutomationRole::Button);
    REQUIRE(ui::automation_providers(b).toggle->toggle_next());
    REQUIRE(ui::automation_providers(b).toggle->toggle_value() == ToggleState::On);
    REQUIRE(clicks == 0);
    b.set_enabled(false);
    REQUIRE_FALSE(ui::automation_providers(b).toggle->toggle_next());
}

TEST_CASE("ui::ToggleButton binds checked and reports the user's changes", "[toggle-button][spec]")
{
    Property<std::optional<bool>> checked{std::optional<bool>{true}};
    std::optional<bool>           reported;
    ui::Control<ui::ToggleButton> raw;
    auto                          node = ui::build(ui::ToggleButton{
        .text               = "Bold",
        .checked            = bind(checked),
        .on_checked_changed = [&](std::optional<bool> v) { reported = v; },
        .control            = raw,
    });
    REQUIRE(raw != nullptr);
    REQUIRE(raw->checked() == true);
    REQUIRE_FALSE(reported.has_value()); // seeding the initial value raises nothing
    checked = std::optional<bool>{false};
    REQUIRE(raw->checked() == false);
    REQUIRE_FALSE(reported.has_value()); // a bound write isn't the user's change
    REQUIRE(ui::automation_providers(*raw).toggle->toggle_next());
    REQUIRE(reported == true);
    REQUIRE(checked.get() == true);
}

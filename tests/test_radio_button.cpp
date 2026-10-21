// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/RadioGroup.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <optional>
#include <string>
#include <utility>
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

RadioButtonControl make_radio(std::string text = "Option")
{
    RadioButtonControl r{std::move(text)};
    r.arrange({100, 100, 120, 24});
    return r;
}
const Point Inside{110, 110};
} // namespace

TEST_CASE("New radio button is unchecked and Normal", "[radio]")
{
    RadioButtonControl r = make_radio();
    REQUIRE_FALSE(r.checked());
    REQUIRE(ui::testing::visual_state(r) == ButtonState::Normal);
}

TEST_CASE("A standalone radio button (no group) can be checked but not unchecked by reclicking", "[radio]")
{
    RadioButtonControl r = make_radio();

    r.handle_pointer(down(Inside));
    r.handle_pointer(up(Inside));
    REQUIRE(r.checked());

    r.handle_pointer(down(Inside));
    r.handle_pointer(up(Inside));
    REQUIRE(r.checked()); // still checked: reclicking a checked radio is a no-op
}

TEST_CASE("Space activates a focused radio button", "[radio][command]")
{
    RadioButtonControl r = make_radio();
    ui::testing::set_focused(r, true);

    r.handle_key({Key::Space, KeyAction::Down});
    REQUIRE(r.is_pressed());
    r.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(r.checked());

    // Enter does not activate a radio button.
    RadioButtonControl r2 = make_radio();
    ui::testing::set_focused(r2, true);
    r2.handle_key({Key::Enter, KeyAction::Down});
    r2.handle_key({Key::Enter, KeyAction::Up});
    REQUIRE_FALSE(r2.checked());
}

TEST_CASE("Disabled radio button ignores input", "[radio][state]")
{
    RadioButtonControl r = make_radio();
    r.set_enabled(false);

    r.handle_pointer(down(Inside));
    r.handle_pointer(up(Inside));

    REQUIRE_FALSE(r.checked());
    REQUIRE(ui::testing::visual_state(r) == ButtonState::Disabled);
}

TEST_CASE("RadioGroup enforces exactly one selection among its members", "[radio][group]")
{
    RadioGroup         group;
    RadioButtonControl a = make_radio("A");
    RadioButtonControl b = make_radio("B");
    RadioButtonControl c = make_radio("C");
    group.add(&a);
    group.add(&b);
    group.add(&c);

    a.handle_pointer(down(Inside));
    a.handle_pointer(up(Inside));
    REQUIRE(a.checked());
    REQUIRE_FALSE(b.checked());
    REQUIRE_FALSE(c.checked());
    REQUIRE(group.selected() == &a);

    b.handle_pointer(down(Inside));
    b.handle_pointer(up(Inside));
    REQUIRE_FALSE(a.checked()); // selecting b unchecks a
    REQUIRE(b.checked());
    REQUIRE_FALSE(c.checked());
    REQUIRE(group.selected() == &b);
}

TEST_CASE("RadioGroup fires on_change once per actual selection change, not on a no-op reclick", "[radio][group]")
{
    RadioGroup                       group;
    RadioButtonControl               a = make_radio("A");
    RadioButtonControl               b = make_radio("B");
    std::vector<RadioButtonControl*> changes;
    group.add(&a);
    group.add(&b);
    group.on_change([&](RadioButtonControl* selected) { changes.push_back(selected); });

    a.handle_pointer(down(Inside));
    a.handle_pointer(up(Inside));
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0] == &a);

    a.handle_pointer(down(Inside));
    a.handle_pointer(up(Inside));
    REQUIRE(changes.size() == 1);

    b.handle_pointer(down(Inside));
    b.handle_pointer(up(Inside));
    REQUIRE(changes.size() == 2);
    REQUIRE(changes[1] == &b);
}

TEST_CASE("A member constructed already checked becomes the group's initial selection", "[radio][group]")
{
    RadioGroup         group;
    RadioButtonControl a = make_radio("A");
    RadioButtonControl b{"B", /*checked=*/true};

    group.add(&a);
    group.add(&b);

    REQUIRE(group.selected() == &b);
}

TEST_CASE("RadioGroup::select() can be driven programmatically, not just by click", "[radio][group]")
{
    RadioGroup         group;
    RadioButtonControl a = make_radio("A");
    RadioButtonControl b = make_radio("B");
    group.add(&a);
    group.add(&b);

    group.select(&b);
    REQUIRE(b.checked());
    REQUIRE_FALSE(a.checked());
    REQUIRE(group.selected() == &b);
}

// Windows1x's checked background is COLOR_WINDOW white and Aero's well is #F4F4F4, so the
// ring fills from `background` and the dot uses `text`. The dot is checked against both.
TEST_CASE("A checked RadioButton's dot is visible against its own ring, under every theme",
          "[radio][render][regression]")
{
    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme          theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        HeadlessTextMeasurer measurer;
        RadioButtonControl   radio{"On"};
        radio.set_checked(true);
        const Size d = radio.measure(Size{200.0f, Unbounded}, measurer, theme);
        radio.arrange(Rect{0.0f, 0.0f, 200.0f, d.height});

        TestRenderer renderer;
        radio.render(renderer, theme);

        // The dot is the last filled shape, the ring's fill the first. Luna's gradient well
        // leaves `ring` unset, so its dot is checked against the page only.
        std::optional<Color> ring;
        bool                 found = false;
        Color                dot{};
        for (const auto& cmd : renderer.commands)
        {
            Color fill{};
            if (std::holds_alternative<FillEllipse>(cmd))
            {
                fill = std::get<FillEllipse>(cmd).color;
            }
            else if (std::holds_alternative<FillRoundedRect>(cmd))
            {
                fill = std::get<FillRoundedRect>(cmd).color;
            }
            else
            {
                continue;
            }
            if (found && !ring)
            {
                ring = dot;
            }
            dot   = fill;
            found = true;
        }
        REQUIRE(found);
        CHECK(dot.a > 0.0f);
        // A Fluent 1 dot is the page's own white, which is fine: it sits on an accent disc.
        if (ring)
        {
            CHECK(dot != *ring);
        }
        if (!ring || ring->a < 1.0f)
        {
            CHECK(dot != theme.colors().background);
            CHECK(dot != theme.colors().field);
        }
    }
}

// Under Fluent 2 a checked ring is the accent with an on-accent text color dot in every
// state, even Pressed and Disabled where the accent button's own label drops a rung.
TEST_CASE("A Fluent RadioButton's ring is filled, and its checked dot is the on-accent text color",
          "[radio][render][theme]")
{
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme          theme = Theme::fluent(palette);
        HeadlessTextMeasurer measurer;

        auto first_and_last_fill = [&](RadioButtonControl& radio)
        {
            const Size d = radio.measure(Size{200.0f, Unbounded}, measurer, theme);
            radio.arrange(Rect{0.0f, 0.0f, 200.0f, d.height});
            TestRenderer renderer;
            radio.render(renderer, theme);
            std::vector<FillRoundedRect> fills;
            for (const auto& cmd : renderer.commands)
            {
                if (std::holds_alternative<FillRoundedRect>(cmd))
                {
                    fills.push_back(std::get<FillRoundedRect>(cmd));
                }
            }
            REQUIRE_FALSE(fills.empty());
            return std::pair{fills.front(), fills.back()};
        };

        RadioButtonControl unchecked{"Off"};
        const auto [off_ring, off_last] = first_and_last_fill(unchecked);
        CHECK(off_ring.color == palette.control_alt_fill_secondary);
        CHECK(off_last.color == off_ring.color); // no dot

        RadioButtonControl checked{"On"};
        checked.set_checked(true);
        const auto [on_ring, on_dot] = first_and_last_fill(checked);
        CHECK(on_ring.color == theme.accent_button_normal.background);
        CHECK(on_ring.bounds.width > on_dot.bounds.width);
        CHECK(on_dot.color == palette.text_on_accent_primary);

        RadioButtonControl disabled{"On"};
        disabled.set_checked(true);
        disabled.set_enabled(false);
        CHECK(first_and_last_fill(disabled).second.color == palette.text_on_accent_primary);
    }
}

TEST_CASE("a RadioGroup and its buttons may be destroyed in either order", "[radio][group]")
{
    SECTION("the group first")
    {
        RadioButtonControl a{"A", true};
        RadioButtonControl b{"B"};
        {
            RadioGroup group;
            group.add(&a);
            group.add(&b);
        }
        ui::automation_providers(b).selection_item->select_item(); // asks no group: there is none left to ask
        CHECK(a.checked());                                        // and nothing unchecked the other one
    }
    SECTION("a button first")
    {
        RadioGroup         group;
        RadioButtonControl a{"A", true};
        group.add(&a);
        {
            RadioButtonControl b{"B"};
            group.add(&b);
            group.select(&b);
            CHECK(group.selected() == &b);
        }
        CHECK(group.selected() == nullptr); // the selected member is gone
        group.select(&a);                   // walks the members that are left
        CHECK(a.checked());
    }
    SECTION("a button moves to the group it is added to")
    {
        RadioGroup         first;
        RadioGroup         second;
        RadioButtonControl a{"A"};
        first.add(&a);
        second.add(&a);
        CHECK_FALSE(first.remove(&a));
        CHECK(second.remove(&a));
    }
}

namespace
{
// The color the last solid rect covering device pixel (x, y) at 96 DPI paints, if any.
std::optional<Color> pixel(const TestRenderer& renderer, int x, int y)
{
    const Point          center{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
    std::optional<Color> color;
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd); f && f->bounds.contains(center))
        {
            color = f->color;
        }
    }
    return color;
}
} // namespace

// DrawFrameControl(DFCS_BUTTONRADIO): BTNSHADOW/BTNHIGHLIGHT outside 3DDKSHADOW/3DLIGHT, a
// COLOR_WINDOW well that turns BTNFACE while the button is held (DFCS_PUSHED) or disabled
// (DFCS_INACTIVE), and a WINDOWTEXT dot that turns BTNSHADOW when disabled.
TEST_CASE("A classic radio is the era's round well in every state", "[radio][render][retro]")
{
    const Theme         theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    const Color         face  = theme.colors().background;
    const Color         well  = theme.colors().field;
    const Color         ink   = theme.colors().field_text;
    const ButtonVisual& ramp  = ui::detail::controls(theme).radio_ring.normal.off;

    // make_radio()'s 24 px row centers the 13 px box on y 105.5, which lands on pixel row 106.
    constexpr int X = 100;
    constexpr int Y = 106;
    struct Case
    {
        const char* name;
        bool        checked;
        bool        pressed;
        bool        enabled;
    };
    for (const Case& c :
         {Case{"rest", false, false, true},
          Case{"checked", true, false, true},
          Case{"held", false, true, true},
          Case{"checked and held", true, true, true},
          Case{"disabled", false, false, false},
          Case{"checked and disabled", true, false, false}})
    {
        CAPTURE(c.name);
        RadioButtonControl r = make_radio();
        r.set_checked(c.checked);
        r.set_enabled(c.enabled);
        if (c.pressed)
        {
            r.handle_pointer(down(Inside));
            REQUIRE(ui::testing::visual_state(r) == ButtonState::Pressed);
        }
        TestRenderer renderer;
        r.render(renderer, theme);

        // Round, but no ellipse: every pixel is placed.
        CHECK(renderer.count_of<FillEllipse>() == 0);
        CHECK(renderer.count_of<StrokeEllipse>() == 0);

        CHECK(pixel(renderer, X + 5, Y) == ramp.bevel_shadow);          // outer, top
        CHECK(pixel(renderer, X + 1, Y + 5) == ramp.bevel_shadow);      // outer, left
        CHECK(pixel(renderer, X + 5, Y + 1) == ramp.bevel_dark_shadow); // inner, top
        CHECK(pixel(renderer, X + 11, Y + 5) == ramp.bevel_light);      // inner, right
        CHECK(pixel(renderer, X + 12, Y + 5) == ramp.bevel_highlight);  // outer, right
        CHECK(pixel(renderer, X + 5, Y + 11) == ramp.bevel_highlight);  // outer, bottom
        CHECK_FALSE(pixel(renderer, X, Y).has_value());                 // outside the ring

        const bool grayed = c.pressed || !c.enabled;
        CHECK(pixel(renderer, X + 4, Y + 3) == (grayed ? face : well));
        if (c.checked)
        {
            CHECK(pixel(renderer, X + 6, Y + 5) == (c.enabled ? ink : ramp.bevel_shadow));
        }
        else
        {
            CHECK(pixel(renderer, X + 6, Y + 5) == (grayed ? face : well));
        }
    }
}

TEST_CASE("A classic radio and check box have no hover state", "[radio][render][retro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(theme).radio_ring.hover.on == ui::detail::controls(theme).radio_ring.normal.on);
    CHECK(ui::detail::controls(theme).radio_ring.hover.off == ui::detail::controls(theme).radio_ring.normal.off);
    CHECK(ui::detail::controls(theme).check_box_box.hover.off == ui::detail::controls(theme).check_box_box.normal.off);
    // Held, both wells are BTNFACE.
    CHECK(ui::detail::controls(theme).check_box_box.pressed.off.background == theme.colors().background);
    CHECK(ui::detail::controls(theme).radio_ring.pressed.on.background == theme.colors().background);
}

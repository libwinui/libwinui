// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
PointerEvent leave()
{
    return {{}, PointerButton::None, PointerAction::Leave};
}
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
KeyEvent key_up(Key k)
{
    return {k, KeyAction::Up};
}

ButtonControl make_button()
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40}); // bounds x:[100,200) y:[100,140)
    return b;
}
const Point Inside{150, 120};
const Point Outside{10, 10};
} // namespace

TEST_CASE("New button is Normal and enabled", "[button]")
{
    ButtonControl b = make_button();
    REQUIRE(b.enabled());
    REQUIRE_FALSE(b.focused());
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Normal);
}

TEST_CASE("Hover state follows the pointer", "[button][state]")
{
    ButtonControl b = make_button();

    b.handle_pointer(move(Inside));
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Hover);

    b.handle_pointer(move(Outside));
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Normal);

    b.handle_pointer(move(Inside));
    b.handle_pointer(leave());
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Normal);
}

TEST_CASE("Press/release inside fires the click command", "[button][command]")
{
    ButtonControl b      = make_button();
    int           clicks = 0;
    b.on_click([&] { ++clicks; });

    b.handle_pointer(move(Inside));
    b.handle_pointer(down(Inside));
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Pressed);
    REQUIRE(b.is_pressed());

    b.handle_pointer(up(Inside));
    REQUIRE(clicks == 1);
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Hover); // still hovered after release
}

TEST_CASE("Release outside the button cancels the click", "[button][command]")
{
    ButtonControl b      = make_button();
    int           clicks = 0;
    b.on_click([&] { ++clicks; });

    b.handle_pointer(down(Inside));
    b.handle_pointer(move(Outside));                               // drag off
    REQUIRE(ui::testing::visual_state(b) != ButtonState::Pressed); // press visual drops when not over
    b.handle_pointer(up(Outside));

    REQUIRE(clicks == 0);
}

TEST_CASE("Keyboard activation requires focus; Enter and Space both work", "[button][command]")
{
    ButtonControl b      = make_button();
    int           clicks = 0;
    b.on_click([&] { ++clicks; });

    b.handle_key(key_down(Key::Enter));
    b.handle_key(key_up(Key::Enter));
    REQUIRE(clicks == 0);

    ui::testing::set_focused(b, true);
    b.handle_key(key_down(Key::Enter));
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Pressed);
    b.handle_key(key_up(Key::Enter));
    REQUIRE(clicks == 1);

    b.handle_key(key_down(Key::Space));
    b.handle_key(key_up(Key::Space));
    REQUIRE(clicks == 2);

    b.handle_key(key_down(Key::Tab));
    b.handle_key(key_up(Key::Tab));
    REQUIRE(clicks == 2);
}

TEST_CASE("Losing focus cancels an in-progress keyboard press", "[button][state]")
{
    ButtonControl b      = make_button();
    int           clicks = 0;
    b.on_click([&] { ++clicks; });

    ui::testing::set_focused(b, true);
    b.handle_key(key_down(Key::Space));
    REQUIRE(b.is_pressed());

    ui::testing::set_focused(b, false);
    REQUIRE_FALSE(b.is_pressed());
    b.handle_key(key_up(Key::Space)); // arrives after focus loss
    REQUIRE(clicks == 0);
}

TEST_CASE("Disabled button ignores input and reports Disabled", "[button][state]")
{
    ButtonControl b      = make_button();
    int           clicks = 0;
    b.on_click([&] { ++clicks; });
    b.set_enabled(false);

    b.handle_pointer(move(Inside));
    b.handle_pointer(down(Inside));
    b.handle_pointer(up(Inside));
    ui::testing::set_focused(b, true);
    b.handle_key(key_down(Key::Enter));
    b.handle_key(key_up(Key::Enter));

    REQUIRE(clicks == 0);
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Disabled);
}

TEST_CASE("Focus is orthogonal to visual_state - a focused-not-hovered button still reports Normal", "[button][state]")
{
    ButtonControl b = make_button();
    ui::testing::set_focused(b, true);
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Normal);
    REQUIRE(b.focused());

    b.handle_pointer(move(Inside)); // still focused, now also hovered: keeps its hover look
    REQUIRE(ui::testing::visual_state(b) == ButtonState::Hover);
    REQUIRE(b.focused());
}

TEST_CASE("Button defaults to ButtonVariant::Neutral and set_style() round-trips", "[button][style]")
{
    ButtonControl b = make_button();
    REQUIRE(b.variant() == ButtonVariant::Neutral);

    b.set_variant(ButtonVariant::Accent);
    REQUIRE(b.variant() == ButtonVariant::Accent);

    b.set_variant(ButtonVariant::Subtle);
    REQUIRE(b.variant() == ButtonVariant::Subtle);
}

TEST_CASE("ButtonVariant::Hyperlink is the subtle button with link-colored text", "[button][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    ButtonControl link{"Informational link"};
    link.set_variant(ButtonVariant::Hyperlink);
    const Size desired = link.measure({Unbounded, Unbounded}, m, theme);
    link.arrange({0.0f, 0.0f, desired.width, desired.height});
    REQUIRE(link.automation_role() == AutomationRole::Hyperlink);

    TestRenderer r;
    link.render(r, theme);
    bool found = false;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "Informational link")
        {
            REQUIRE(t->color == theme.colors().link);
            found = true;
        }
    }
    REQUIRE(found);

    link.set_enabled(false);
    TestRenderer disabled;
    link.render(disabled, theme);
    for (const auto& c : disabled.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr)
        {
            REQUIRE(t->color == theme.subtle_button_disabled.text);
        }
    }
}

TEST_CASE("A content padding override sizes the button, not only places its content", "[button][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    ButtonControl padded{"Text"};
    const Size    normal = padded.measure({Unbounded, Unbounded}, m, theme);
    padded.set_padding(Thickness{0.0f});
    const Size tight = padded.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(normal.width - tight.width, WithinAbs(theme.padding.horizontal(), 1e-3));
    REQUIRE_THAT(normal.height - tight.height, WithinAbs(theme.padding.vertical(), 1e-3));
}

TEST_CASE("A hyperlink's text follows HyperlinkButton's four foregrounds", "[button][render][theme]")
{
    HeadlessTextMeasurer m;

    const auto text_in = [&](const Theme& theme, bool hover, bool press, bool enabled)
    {
        ButtonControl link{"Link"};
        link.set_variant(ButtonVariant::Hyperlink);
        const Size desired = link.measure({Unbounded, Unbounded}, m, theme);
        link.arrange({0.0f, 0.0f, desired.width, desired.height});
        const Point center = link.bounds().center();
        if (hover)
        {
            link.handle_pointer({center, PointerButton::None, PointerAction::Move});
        }
        if (press)
        {
            link.handle_pointer({center, PointerButton::Left, PointerAction::Down});
        }
        link.set_enabled(enabled);
        TestRenderer r;
        link.render(r, theme);
        for (const auto& c : r.commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&c))
            {
                return t->color;
            }
        }
        return Color{};
    };

    // Fluent 2, from the live accent ramp: Dark2 / Dark3 / Dark1 in Light.
    OsThemeSnapshot os;
    os.accent_ramp.dark1 = Color::from_argb(0xFF00548C);
    os.accent_ramp.dark2 = Color::from_argb(0xFF003A66);
    os.accent_ramp.dark3 = Color::from_argb(0xFF00223D);
    const Theme fluent   = resolve_theme(ThemeRequest{}, os);
    REQUIRE(text_in(fluent, false, false, true) == os.accent_ramp.dark2);
    REQUIRE(text_in(fluent, true, false, true) == os.accent_ramp.dark3);
    REQUIRE(text_in(fluent, true, true, true) == os.accent_ramp.dark1);
    REQUIRE(text_in(fluent, false, false, false) == Palette::fluent_light().accent_text_disabled);

    // Dark: Light3 / Light3 / Light2.
    os.dark                 = true;
    const Theme fluent_dark = resolve_theme(ThemeRequest{}, os);
    REQUIRE(text_in(fluent_dark, false, false, true) == os.accent_ramp.light3);
    REQUIRE(text_in(fluent_dark, true, true, true) == os.accent_ramp.light2);

    // Windows 10 grays the link under the pointer instead.
    os.dark               = false;
    const Theme windows10 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, os);
    REQUIRE(text_in(windows10, true, false, true) == Color::from_argb(0x99000000));
    REQUIRE(text_in(windows10, true, true, true) == Color::from_argb(0x66000000));

    // An era without a hyperlink button keeps its one link color.
    const Theme luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, os);
    REQUIRE(text_in(luna, true, false, true) == luna.colors().link);
}

TEST_CASE("A theme-resource background fills the button at rest and follows the theme", "[button][render][theme]")
{
    const Theme          light    = Theme::light();
    const Theme          dark     = Theme::fluent(Palette::fluent_dark());
    constexpr ThemeColor resource = [](const Theme& t) { return t.palette.text_fill_secondary; };
    REQUIRE_FALSE(resource(light) == resource(dark));
    const auto has_fill = [](const TestRenderer& r, Color color)
    {
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f != nullptr && f->color == color)
            {
                return true;
            }
        }
        return false;
    };

    ui::Control<ui::Button> button;
    auto                    built = ui::build(ui::Button{.text = "Themed", .background = resource, .control = button});
    button->arrange({0, 0, 100, 32});

    TestRenderer in_light;
    button->render(in_light, light);
    CHECK(has_fill(in_light, resource(light)));
    TestRenderer in_dark;
    button->render(in_dark, dark);
    CHECK(has_fill(in_dark, resource(dark)));

    // A fixed color replaces the resource.
    button->set_background(colors::Black);
    TestRenderer fixed;
    button->render(fixed, dark);
    CHECK(has_fill(fixed, colors::Black));
    CHECK_FALSE(has_fill(fixed, resource(dark)));
}

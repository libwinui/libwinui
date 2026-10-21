// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ColorPicker.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
bool approx_color(const Color& a, const Color& b, float eps = 1e-3f)
{
    return std::abs(a.r - b.r) < eps && std::abs(a.g - b.g) < eps && std::abs(a.b - b.b) < eps && std::abs(a.a - b.a) < eps;
}

// With the defaults and alpha off the tab order is [0-2] the sliders,
// [3] the dropdown, [4-6] the channel boxes and [7] hex.
std::vector<Element*> focusables(ColorPickerControl& picker)
{
    std::vector<Element*> out;
    picker.collect_focusables(out);
    return out;
}

void lay_out(ColorPickerControl& picker)
{
    HeadlessTextMeasurer m;
    const Size           desired = picker.measure({320.0f, Unbounded}, m, Theme::default_light());
    picker.arrange({0, 0, desired.width, desired.height});
}

void retype(TextBoxControl& box, const std::string& text)
{
    ui::testing::set_focused(box, true);
    while (!box.text().empty())
    {
        box.handle_key({Key::Backspace, KeyAction::Down});
        box.handle_key({Key::Backspace, KeyAction::Up});
    }
    // One code point a keystroke, as WM_CHAR delivers them.
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        box.handle_text(std::string_view{text}.substr(i, next_code_point(text, i) - i));
    }
}

void press_enter(TextBoxControl& box)
{
    box.handle_key({Key::Enter, KeyAction::Down});
    box.handle_key({Key::Enter, KeyAction::Up});
}
} // namespace

TEST_CASE("A default-constructed ColorPicker is black with alpha disabled", "[color_picker]")
{
    ColorPickerControl picker;
    REQUIRE(approx_color(picker.color(), colors::Black));
    REQUIRE_FALSE(picker.alpha_enabled());
}

TEST_CASE("An initial color seeds the channel sliders, round-tripping through HSV", "[color_picker]")
{
    ColorPickerControl picker{Color::from_rgba(0, 255, 0)}; // pure green
    REQUIRE(approx_color(picker.color(), Color::from_rgba(0, 255, 0)));
}

TEST_CASE("set_color with alpha disabled always forces alpha to 1", "[color_picker]")
{
    ColorPickerControl picker{colors::Black, /*alpha_enabled=*/false};
    picker.set_color(colors::Black.with_alpha(0.3f));
    REQUIRE(picker.color().a == 1.0f);
}

TEST_CASE("set_color fires on_color_changed only when the color actually changes", "[color_picker]")
{
    ColorPickerControl picker;
    int                calls = 0;
    picker.on_color_changed([&](Color) { ++calls; });

    picker.set_color(Color::from_rgba(255, 0, 0));
    REQUIRE(calls == 1);

    picker.set_color(Color::from_rgba(255, 0, 0)); // same value again: no-op
    REQUIRE(calls == 1);

    picker.set_color(Color::from_rgba(0, 0, 255));
    REQUIRE(calls == 2);
}

TEST_CASE("The defaults show everything but the More button; alpha adds its slider and box", "[color_picker][focus]")
{
    ColorPickerControl without_alpha{colors::Black, false};
    REQUIRE(focusables(without_alpha).size() == 8); // 3 sliders + dropdown + 3 channels + hex
    CHECK(dynamic_cast<ComboBoxControl*>(focusables(without_alpha)[3]) != nullptr);
    CHECK(dynamic_cast<TextBoxControl*>(focusables(without_alpha)[7]) != nullptr);

    ColorPickerControl with_alpha{colors::Black, true};
    const auto         parts = focusables(with_alpha);
    REQUIRE(parts.size() == 10);
    CHECK(dynamic_cast<SliderControl*>(parts[3]) != nullptr);  // the alpha slider, after value
    CHECK(dynamic_cast<TextBoxControl*>(parts[8]) != nullptr); // the alpha box, before hex
}

TEST_CASE("Dragging the value slider updates color() and fires on_color_changed", "[color_picker][input]")
{
    ColorPickerControl picker;
    lay_out(picker);

    int calls = 0;
    picker.on_color_changed([&](Color) { ++calls; });

    // The default seed is black, so only the value slider visibly moves color().
    auto*       val        = static_cast<SliderControl*>(focusables(picker)[2]);
    const Rect  val_bounds = val->bounds();
    const Point p{val_bounds.right() - 1.0f, val_bounds.top() + val_bounds.height * 0.5f};
    picker.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    picker.handle_pointer({p, PointerButton::Left, PointerAction::Up});

    REQUIRE(calls >= 1);
    REQUIRE(approx_color(picker.color(), colors::White, 0.05f));

    // And the text boxes followed: a pixel inside the edge snaps to 100 at the default step of 1.
    CHECK(static_cast<TextBoxControl*>(focusables(picker)[4])->text() == "255");
    CHECK(static_cast<TextBoxControl*>(focusables(picker)[7])->text() == "#FFFFFF");
}

TEST_CASE("A valid hex commit updates the color; an invalid one reverts the text", "[color_picker][input]")
{
    ColorPickerControl picker;
    auto*              hex = static_cast<TextBoxControl*>(focusables(picker)[7]);

    int calls = 0;
    picker.on_color_changed([&](Color) { ++calls; });

    hex->set_text("#00FF00");
    ui::testing::set_focused(*hex, true);
    press_enter(*hex);

    REQUIRE(calls == 1);
    REQUIRE(approx_color(picker.color(), Color::from_rgba(0, 255, 0)));

    hex->set_text("not a color");
    press_enter(*hex);

    REQUIRE(calls == 1);                                                // the invalid commit fired nothing
    REQUIRE(approx_color(picker.color(), Color::from_rgba(0, 255, 0))); // color unchanged
    REQUIRE(hex->text() == "#00FF00");                                  // text reverted to the last-good value
}

TEST_CASE("Disabled ColorPicker cascades to every owned part", "[color_picker][state]")
{
    ColorPickerControl picker;
    picker.set_more_button_visible(true);
    picker.set_enabled(false);

    for (Element* e : focusables(picker))
    {
        REQUIRE_FALSE(e->focusable());
    }
}

TEST_CASE("Render draws one gradient track per shown channel, plus the preview swatch", "[color_picker][render]")
{
    ColorPickerControl without_alpha{Color::from_rgba(10, 20, 30), false};
    lay_out(without_alpha);
    TestRenderer r1;
    without_alpha.render(r1, Theme::light());
    // Each shown channel draws one gradient track; alpha_slider_ draws none here.
    REQUIRE(r1.count_of<FillRoundedRectGradient>() == 3);
    const auto swatch = [](const TestRenderer& r, const Color& c)
    {
        std::size_t n = 0;
        for (const RenderCommand& command : r.commands)
        {
            const auto* fill = std::get_if<FillRoundedRect>(&command);
            n += fill && approx_color(fill->color, c, 0.01f) ? 1 : 0;
        }
        return n;
    };
    CHECK(swatch(r1, Color::from_rgba(10, 20, 30)) == 1);

    ColorPickerControl with_alpha{colors::Black, true};
    lay_out(with_alpha);
    TestRenderer r2;
    with_alpha.render(r2, Theme::light());
    REQUIRE(r2.count_of<FillRoundedRectGradient>() == 4); // +1 for the now-shown alpha channel
    REQUIRE(r2.count_of<FillRectPattern>() == 1);         // alpha's checkerboard-behind
}

TEST_CASE("Each visibility switch takes its part out of layout, Tab order and drawing", "[color_picker][visibility]")
{
    ColorPickerControl picker{colors::Black, true};
    lay_out(picker);
    REQUIRE(focusables(picker).size() == 10);

    picker.set_hex_input_visible(false);
    CHECK(focusables(picker).size() == 9);
    picker.set_alpha_text_input_visible(false);
    CHECK(focusables(picker).size() == 8);
    picker.set_alpha_slider_visible(false);
    CHECK(focusables(picker).size() == 7);
    picker.set_color_channel_text_input_visible(false); // the dropdown and three boxes
    CHECK(focusables(picker).size() == 3);
    picker.set_color_slider_visible(false);
    CHECK(focusables(picker).size() == 2);
    picker.set_spectrum_visible(false); // hue and saturation
    CHECK(focusables(picker).empty());

    // With the spectrum gone the preview spans the row at 44px tall.
    lay_out(picker);
    CHECK(picker.bounds().height == 44.0f);
    picker.set_preview_visible(false);
    lay_out(picker);
    CHECK(picker.bounds().height == 0.0f);
    TestRenderer nothing;
    picker.render(nothing, Theme::light());
    CHECK(nothing.commands.empty());

    picker.set_hex_input_visible(true);
    picker.set_color_slider_visible(true);
    const auto parts = focusables(picker);
    REQUIRE(parts.size() == 2);
    CHECK(dynamic_cast<SliderControl*>(parts[0]) != nullptr);
    CHECK(dynamic_cast<TextBoxControl*>(parts[1]) != nullptr);
}

TEST_CASE("The More button hides the text entry section until it is clicked", "[color_picker][visibility]")
{
    ColorPickerControl picker;
    picker.set_more_button_visible(true);
    lay_out(picker);
    const float collapsed = picker.bounds().height;

    auto parts = focusables(picker);
    REQUIRE(parts.size() == 4); // three sliders and More
    auto* more = dynamic_cast<ButtonControl*>(parts[3]);
    REQUIRE(more != nullptr);
    CHECK_FALSE(picker.more_expanded());

    const Point p = more->bounds().center();
    picker.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    picker.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    CHECK(picker.more_expanded());
    CHECK(focusables(picker).size() == 9); // + the dropdown, channels and hex
    lay_out(picker);
    CHECK(picker.bounds().height > collapsed);

    picker.set_more_button_visible(false);
    CHECK(focusables(picker).size() == 8);
}

TEST_CASE("alpha_enabled is live: turning it off makes the color opaque", "[color_picker][visibility]")
{
    ColorPickerControl picker{Color::from_rgba(255, 0, 0).with_alpha(0.5f), true};
    REQUIRE(picker.color().a == 0.5f);
    int calls = 0;
    picker.on_color_changed([&](Color) { ++calls; });

    picker.set_alpha_enabled(false);
    CHECK(picker.color().a == 1.0f);
    CHECK(calls == 1);
    CHECK(focusables(picker).size() == 8);
    CHECK(static_cast<TextBoxControl*>(focusables(picker)[7])->text() == "#FF0000");

    picker.set_alpha_enabled(true);
    CHECK(picker.color().a == 1.0f); // back on, from opaque
    CHECK(calls == 1);
    CHECK(focusables(picker).size() == 10);
}

TEST_CASE("A typed channel value applies as it is typed; Enter reverts invalid text", "[color_picker][input]")
{
    ColorPickerControl picker;
    lay_out(picker);
    auto* red = static_cast<TextBoxControl*>(focusables(picker)[4]);
    auto* hex = static_cast<TextBoxControl*>(focusables(picker)[7]);
    REQUIRE(red->text() == "0");

    int calls = 0;
    picker.on_color_changed([&](Color) { ++calls; });

    retype(*red, "255");
    CHECK(approx_color(picker.color(), Color::from_rgba(255, 0, 0)));
    CHECK(red->text() == "255"); // the box being typed into is never rewritten under the caret
    CHECK(hex->text() == "#FF0000");
    CHECK(calls == 3); // 2, 25, 255

    // On the way to "999", "9" and "99" are valid and apply; the third 9 is out of range and is
    // left as typed without touching the color.
    retype(*red, "999");
    CHECK(approx_color(picker.color(), Color::from_rgba(99, 0, 0)));
    CHECK(red->text() == "999");
    press_enter(*red);
    CHECK(red->text() == "99");
}

TEST_CASE("HSV mode relabels the channel boxes and writes hue straight to its slider", "[color_picker][input]")
{
    ColorPickerControl picker; // black: saturation 0, so RGB has no hue to carry
    lay_out(picker);
    auto  parts = focusables(picker);
    auto* mode  = static_cast<ComboBoxControl*>(parts[3]);
    auto* hue   = static_cast<TextBoxControl*>(parts[4]);

    mode->set_selected_index(1);
    const auto labels = [&]
    {
        TestRenderer renderer;
        picker.render(renderer, Theme::light());
        std::vector<std::string> out;
        for (const RenderCommand& command : renderer.commands)
        {
            if (const auto* run = std::get_if<DrawTextRun>(&command))
            {
                out.push_back(run->text);
            }
        }
        return out;
    };
    const auto drawn = labels();
    CHECK(std::find(drawn.begin(), drawn.end(), "Hue") != drawn.end());
    CHECK(std::find(drawn.begin(), drawn.end(), "Red") == drawn.end());

    retype(*hue, "200");
    CHECK(static_cast<SliderControl*>(parts[0])->value() == 200.0f);
    CHECK(hue->text() == "200");
}

TEST_CASE("The alpha box takes a percentage, with or without the sign", "[color_picker][input]")
{
    ColorPickerControl picker{colors::White, true};
    auto*              alpha = static_cast<TextBoxControl*>(focusables(picker)[8]);
    REQUIRE(alpha->text() == "100%");

    retype(*alpha, "50%");
    CHECK(std::abs(picker.color().a - 0.5f) < 1e-3f);
    retype(*alpha, "25");
    CHECK(std::abs(picker.color().a - 0.25f) < 1e-3f);
    press_enter(*alpha);
    CHECK(alpha->text() == "25%");
}

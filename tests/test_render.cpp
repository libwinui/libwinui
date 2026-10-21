// SPDX-License-Identifier: MIT
#include "render/ImageThemePack.hpp"
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

TEST_CASE("The renderer scopes pop what they pushed, innermost first", "[render]")
{
    TestRenderer r;
    {
        const ClipScope         clip{r, Rect{0.0f, 0.0f, 10.0f, 10.0f}};
        const OpacityLayerScope layer{r, 0.5f};
        const TransformScope    transform{r, 2.0f, Point{1.0f, 1.0f}};
        const ClipScope         ellipse{r, Point{5.0f, 5.0f}, 2.0f, 3.0f};
    }
    REQUIRE(r.count() == 8);
    CHECK(std::holds_alternative<PushClip>(r.commands[0]));
    CHECK(std::holds_alternative<PushOpacityLayer>(r.commands[1]));
    CHECK(std::holds_alternative<PushTransform>(r.commands[2]));
    CHECK(std::holds_alternative<PushClipEllipse>(r.commands[3]));
    CHECK(std::holds_alternative<PopClip>(r.commands[4]));
    CHECK(std::holds_alternative<PopTransform>(r.commands[5]));
    CHECK(std::holds_alternative<PopOpacityLayer>(r.commands[6]));
    CHECK(std::holds_alternative<PopClip>(r.commands[7]));
}

TEST_CASE("Button renders a background rect, an elevation border, and its text (3 commands)", "[render]")
{
    // A neutral Button at rest has an elevation border stroke.
    ButtonControl button{"Hello"};

    TestRenderer renderer;
    button.render(renderer);

    REQUIRE(renderer.count() == 3);
    REQUIRE(std::holds_alternative<FillRoundedRect>(renderer.commands[0]));
    REQUIRE(std::holds_alternative<StrokeRoundedRectGradient>(renderer.commands[1]));
    REQUIRE(std::holds_alternative<DrawTextRun>(renderer.commands[2]));
}

TEST_CASE("Rendered text carries the button label", "[render]")
{
    ButtonControl button{"Save"};
    TestRenderer  renderer;
    button.render(renderer);

    const auto& text = std::get<DrawTextRun>(renderer.commands[2]);
    REQUIRE(text.text == "Save");
}

TEST_CASE("Focused enabled button adds a two-ring focus visual (5 commands)", "[render]")
{
    // The Fluent focus visual is two rings (2px outer, 1px inner), drawn over the tree as its
    // window draws it, so a focused Neutral button is background + border + text + 2 rings.
    ButtonControl button{"Hello"};
    button.arrange({100, 100, 80, 32});
    ui::testing::set_focused(button, true);

    TestRenderer renderer;
    button.render(renderer);
    REQUIRE(renderer.count() == 3); // the button itself draws no ring
    renderer.clear();
    ui::testing::render_with_focus_ring(button, renderer);

    REQUIRE(renderer.count() == 5);
    REQUIRE(renderer.count_of<StrokeRoundedRect>() == 2);
    REQUIRE(renderer.count_of<StrokeRoundedRectGradient>() == 1);
    const auto& outer_ring = std::get<StrokeRoundedRect>(renderer.commands[3]);
    REQUIRE(outer_ring.bounds.x < 100.0f);
    REQUIRE(outer_ring.bounds.width > 80.0f);
    const auto& inner_ring = std::get<StrokeRoundedRect>(renderer.commands[4]);
    REQUIRE(inner_ring.bounds.x < 100.0f);
    REQUIRE(inner_ring.bounds.x > outer_ring.bounds.x);
}

TEST_CASE("Render reflects state-dependent colors from the theme", "[render]")
{
    Theme         theme = Theme::light();
    ButtonControl button{"Hello"};

    TestRenderer normal;
    button.render(normal, theme);
    const auto& normal_bg = std::get<FillRoundedRect>(normal.commands[0]);
    REQUIRE(normal_bg.color == theme.button_normal.background);
    REQUIRE(normal_bg.radius == theme.corner_radius);

    button.set_enabled(false);
    TestRenderer disabled;
    button.render(disabled, theme);
    const auto& disabled_bg = std::get<FillRoundedRect>(disabled.commands[0]);
    REQUIRE(disabled_bg.color == theme.button_disabled.background);
}

// ---- Per-visual border width/draw_border ------------------------------------------------

TEST_CASE("draw_control_chrome strokes a ButtonVisual's own border_width when draw_border is set", "[render][chrome]")
{
    ButtonVisual v{colors::Transparent, colors::White, colors::White};
    v.draw_border  = true;
    v.border_width = 2.0f;

    TestRenderer renderer;
    // The theme-global width (5.0) is passed in but must be overridden by the visual's own.
    draw_control_chrome(renderer, Rect{0, 0, 40, 20}, 0.0f, v, /*border_width=*/5.0f);

    REQUIRE(renderer.count_of<StrokeRoundedRect>() == 1);
    const auto& stroke = std::get<StrokeRoundedRect>(renderer.commands[1]);
    REQUIRE(stroke.stroke_width == 2.0f);
    REQUIRE(stroke.color == colors::White);
}

TEST_CASE("draw_control_chrome draws no border when draw_flat_border_fallback and draw_border are both false",
          "[render][chrome]")
{
    ButtonVisual v{colors::White, colors::Black, colors::Black}; // draw_border defaults to false
    TestRenderer renderer;
    draw_control_chrome(renderer, Rect{0, 0, 40, 20}, 0.0f, v, /*border_width=*/1.0f);

    REQUIRE(renderer.count() == 1); // fill only
    REQUIRE(renderer.count_of<StrokeRoundedRect>() == 0);
}

TEST_CASE("ButtonVisual::resolved_border_width inherits the theme default unless it sets its own", "[render][chrome]")
{
    ButtonVisual inherited{};
    REQUIRE(inherited.resolved_border_width(3.0f) == 3.0f);

    ButtonVisual overridden{};
    overridden.border_width = 0.0f;
    REQUIRE(overridden.resolved_border_width(3.0f) == 0.0f);
}

// ---- Retro theme rendering ----------------------------------------------------------------

TEST_CASE("A Windows9x button fills flat and adds an 8-line bevel border (background + 8 bevel lines + text)",
          "[render][retro]")
{
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    TestRenderer  renderer;
    button.render(renderer, theme);

    REQUIRE(renderer.count() == 10);
    REQUIRE(std::holds_alternative<FillRoundedRect>(renderer.commands[0]));
    REQUIRE(renderer.count_of<StrokeLine>() == 8);
    REQUIRE(std::holds_alternative<DrawTextRun>(renderer.commands[9]));
    REQUIRE_FALSE(std::holds_alternative<FillRoundedRectGradient>(renderer.commands[0]));
}

TEST_CASE("A pressed Windows9x button's bevel swaps to Sunken shading", "[render][retro]")
{
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    button.arrange({0, 0, 50, 20});
    button.handle_pointer({{5, 5}, PointerButton::None, PointerAction::Move});
    button.handle_pointer({{5, 5}, PointerButton::Left, PointerAction::Down});
    REQUIRE(button.is_pressed());

    // Same bounds for both, so only the shading (Raised at rest, Sunken pressed) differs.
    TestRenderer  normal_renderer;
    ButtonControl rest_button{"OK"};
    rest_button.arrange({0, 0, 50, 20});
    rest_button.render(normal_renderer, theme);

    TestRenderer pressed_renderer;
    button.render(pressed_renderer, theme);

    const auto find_top_edge_color = [](const TestRenderer& renderer) -> Color
    {
        constexpr float TopY = 0.5f; // bounds.top() (0) + half the 1px bevel step
        for (const auto& cmd : renderer.commands)
        {
            if (!std::holds_alternative<StrokeLine>(cmd))
            {
                continue;
            }
            const auto& line = std::get<StrokeLine>(cmd);
            if (line.from.y == line.to.y && line.from.y == TopY)
            {
                return line.color;
            }
        }
        FAIL("no top edge StrokeLine found");
        return Color{};
    };

    REQUIRE(find_top_edge_color(normal_renderer) != find_top_edge_color(pressed_renderer));
}

TEST_CASE("A Windows9x command button draws the BF_SOFT edge, not the window-border one", "[render][retro]")
{
    // A command button puts BTNHIGHLIGHT outermost and 3DLIGHT one in, the reverse of a window
    // border or field well. Under 95/98 those are #FFFFFF and #DFDFDF, so the test can see it.
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    button.arrange({0, 0, 50, 20});

    TestRenderer renderer;
    button.render(renderer, theme);

    const auto horizontal_edge_at = [&](float y) -> Color
    {
        for (const auto& cmd : renderer.commands)
        {
            const auto* line = std::get_if<StrokeLine>(&cmd);
            if (line && line->from.y == line->to.y && line->from.y == y)
            {
                return line->color;
            }
        }
        FAIL("no horizontal StrokeLine at that y");
        return Color{};
    };

    REQUIRE(horizontal_edge_at(0.5f) == colors::White);                 // outer: BTNHIGHLIGHT
    REQUIRE(horizontal_edge_at(1.5f) == Color::from_argb(0xFFDFDFDF));  // inner: 3DLIGHT
    REQUIRE(horizontal_edge_at(19.5f) == colors::Black);                // outer: 3DDKSHADOW
    REQUIRE(horizontal_edge_at(18.5f) == Color::from_argb(0xFF808080)); // inner: BTNSHADOW
}

TEST_CASE("A pressed Windows9x button shifts its label one pixel down and right", "[render][retro]")
{
    // Windows 9x's "Pressed Appearance": the caption moves with the bevel.
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});

    const auto label_origin = [&](bool press) -> Point
    {
        ButtonControl button{"OK"};
        button.arrange({0, 0, 50, 20});
        if (press)
        {
            button.handle_pointer({{5, 5}, PointerButton::None, PointerAction::Move});
            button.handle_pointer({{5, 5}, PointerButton::Left, PointerAction::Down});
            REQUIRE(button.is_pressed());
        }
        TestRenderer renderer;
        button.render(renderer, theme);
        for (const auto& cmd : renderer.commands)
        {
            if (const auto* text = std::get_if<DrawTextRun>(&cmd))
            {
                return {text->bounds.x, text->bounds.y};
            }
        }
        FAIL("no DrawTextRun");
        return {};
    };

    const Point rest    = label_origin(false);
    const Point pressed = label_origin(true);
    REQUIRE(pressed.x == rest.x + 1.0f);
    REQUIRE(pressed.y == rest.y + 1.0f);
}

TEST_CASE("A disabled Windows9x button's label asks for the classic embossed two-pass effect", "[render][retro]")
{
    // DrawState(DSS_DISABLED) engraves the label in two passes. TestRenderer records one
    // DrawTextRun carrying the effect, D2DRenderer issues the two draws.
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    button.set_enabled(false);
    button.arrange({0, 0, 50, 20});

    TestRenderer renderer;
    button.render(renderer, theme);

    bool found = false;
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* text = std::get_if<DrawTextRun>(&cmd))
        {
            found = true;
            REQUIRE(text->style.effect.kind == TextEffectKind::Emboss);
            REQUIRE(text->style.effect.highlight == colors::White);
            REQUIRE(text->style.effect.shadow == Color::from_argb(0xFF808080));
        }
    }
    REQUIRE(found);
}

TEST_CASE("A focused Windows9x button draws an inset dotted focus rect, not an outward ring", "[render][retro]")
{
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    button.arrange({0, 0, 50, 20});
    ui::testing::set_focused(button, true);

    TestRenderer renderer;
    button.render(renderer, theme);

    REQUIRE(renderer.count_of<StrokeRoundedRect>() == 0); // no outward Fluent ring
    REQUIRE(renderer.count_of<StrokeLine>() > 6);         // 6 bevel lines + several focus-rect dashes
}

TEST_CASE("A Luna button fills with a gradient and adds no bevel (background + text)", "[render][retro]")
{
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    TestRenderer  renderer;
    button.render(renderer, theme);

    // ButtonControl::render() passes draw_flat_border_fallback = false, so Luna's #003C74 outline only
    // shows because the visual sets draw_border itself.
    REQUIRE(renderer.count() == 3);
    REQUIRE(std::holds_alternative<FillRoundedRectGradient>(renderer.commands[0]));
    REQUIRE(std::holds_alternative<StrokeRoundedRect>(renderer.commands[1]));
    REQUIRE(std::holds_alternative<DrawTextRun>(renderer.commands[2]));

    const auto& gradient = std::get<FillRoundedRectGradient>(renderer.commands[0]);
    REQUIRE(gradient.brush.stops == theme.button_normal.background_gradient->stops);
    REQUIRE(std::get<StrokeRoundedRect>(renderer.commands[1]).color == Color::from_argb(0xFF003C74));
}

TEST_CASE("A hovered Luna button keeps its face and gains a 2px amber ring", "[render][retro]")
{
    const Theme   theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    ButtonControl button{"OK"};
    button.arrange({0, 0, 80, 24});
    button.handle_pointer(PointerEvent{{40, 12}, PointerButton::None, PointerAction::Move});

    TestRenderer renderer;
    button.render(renderer, theme);

    // Fill, border, ring, label: one command more than at rest, and the same fill.
    REQUIRE(renderer.count() == 4);
    REQUIRE(std::get<FillRoundedRectGradient>(renderer.commands[0]).brush.stops ==
            theme.button_normal.background_gradient->stops);
    const auto& ring = std::get<StrokeRoundedRectGradient>(renderer.commands[2]);
    REQUIRE(ring.stroke_width == 2.0f);
    REQUIRE(ring.brush.stops.front().color == Color::from_argb(0xFFFFF0CF));
}

TEST_CASE("A checked Windows9x checkbox draws a sunken bevel well instead of a flat outline", "[render][retro]")
{
    const Theme     theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CheckBoxControl box{"", true};
    TestRenderer    renderer;
    box.render(renderer, theme);

    // fill + 8 bevel lines + glyph, no flat stroke_rounded_rect outline.
    REQUIRE(renderer.count_of<StrokeLine>() == 8);
    REQUIRE(renderer.count_of<StrokeRoundedRect>() == 0);
}

TEST_CASE("An unchecked Windows9x checkbox still fills its sunken well white", "[render][retro]")
{
    const Theme     theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CheckBoxControl box{"", false};
    TestRenderer    renderer;
    box.render(renderer, theme);

    REQUIRE(renderer.count_of<FillRoundedRect>() == 1);
    const auto& fill = std::get<FillRoundedRect>(renderer.commands[0]);
    REQUIRE(fill.color == colors::White);
    REQUIRE(fill.radius == 0.0f); // square well, not the Fluent 2 rounded outline
}

// ---- TextControl follows the theme's type ramp ----------------------------------------------

TEST_CASE("A role-less TextControl renders in the active theme's own era font, not a hardcoded Segoe UI",
          "[render][retro][textblock]")
{
    const Theme  theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    TextControl  label{"Unchecked"};
    TestRenderer renderer;
    label.render(renderer, theme);

    const auto& text = std::get<DrawTextRun>(renderer.commands[0]);
    REQUIRE(text.style.font_family == "Microsoft Sans Serif");
    REQUIRE(text.style.font_size == theme.text_style.font_size);
    // TextControl keeps its own natural left alignment even though the theme's body style
    // (as read directly by Button's label) is centered.
    REQUIRE(text.style.horizontal == TextAlign::Leading);
}

TEST_CASE("TextControl::set_text_style() always wins over the theme's type ramp", "[render][retro][textblock]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    TextControl label{"Explicit"};
    label.set_text_style(TextStyle{20.0f, TextAlign::Trailing, TextAlign::Center, "Consolas"});

    TestRenderer renderer;
    label.render(renderer, theme);

    const auto& text = std::get<DrawTextRun>(renderer.commands[0]);
    REQUIRE(text.style.font_family == "Consolas");
    REQUIRE(text.style.font_size == 20.0f);
}

TEST_CASE("TextControl::set_role() resolves against the theme's matching TypeRamp entry", "[render][textblock]")
{
    Theme theme       = Theme::light();
    theme.type.header = TextStyle{56.0f, TextAlign::Leading, TextAlign::Center, "Segoe UI", FontWeight::Light};
    TextControl label{"Title"};
    label.set_role(TextRole::Header);

    TestRenderer renderer;
    label.render(renderer, theme);

    const auto& text = std::get<DrawTextRun>(renderer.commands[0]);
    REQUIRE(text.style.font_size == 56.0f);
    REQUIRE(text.style.weight == FontWeight::Light);
}

TEST_CASE("A Button with no set_content() still renders its plain text() label (3 commands)", "[render][content]")
{
    ButtonControl button{"Hello"};
    REQUIRE(button.content() == nullptr);

    TestRenderer renderer;
    button.render(renderer);

    REQUIRE(renderer.count() == 3); // background + elevation border + text
    REQUIRE(std::holds_alternative<DrawTextRun>(renderer.commands[2]));
}

TEST_CASE("Button::set_content() replaces the text label with arbitrary content", "[render][content]")
{
    HeadlessTextMeasurer measurer;
    ButtonControl        button;
    button.set_text("ignored once content is set");

    auto*      label   = button.set_content(std::make_unique<TextControl>("Custom"));
    const Size desired = button.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    button.arrange(Rect{0, 0, desired.width, desired.height});

    TestRenderer renderer;
    button.render(renderer);

    // text_ is never read once content() is set.
    REQUIRE(renderer.count() == 3);
    REQUIRE(std::holds_alternative<DrawTextRun>(renderer.commands[2]));
    REQUIRE(std::get<DrawTextRun>(renderer.commands[2]).text == "Custom");
    REQUIRE(label->bounds().width > 0.0f); // arrange_override() actually laid the content out
}

TEST_CASE("An icon+text Button composes IconControl + TextControl through the content model", "[render][content]")
{
    HeadlessTextMeasurer measurer;
    ButtonControl        button;

    auto content = std::make_unique<StackControl>(Orientation::Horizontal);
    content->set_spacing(8.0f); // StackControl::spacing defaults to 0
    auto* icon  = content->add(std::make_unique<IconControl>(glyphs::Accept));
    auto* label = content->add(std::make_unique<TextControl>("Save"));
    button.set_content(std::move(content));

    const Size desired = button.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    button.arrange(Rect{0, 0, desired.width, desired.height});

    TestRenderer renderer;
    button.render(renderer);

    REQUIRE(renderer.count() == 4);
    REQUIRE(renderer.count_of<DrawTextRun>() == 2);
    REQUIRE(icon->bounds().width > 0.0f);
    REQUIRE(label->bounds().left() > icon->bounds().right()); // laid out left-to-right
}

// ---- New render primitives ----------------------------------------------------------------

TEST_CASE("FillEllipse/StrokeEllipse round-trip through TestRenderer and describe()", "[render][ellipse]")
{
    TestRenderer renderer;
    renderer.fill_ellipse({10.0f, 20.0f}, 5.0f, 6.0f, colors::Black);
    renderer.stroke_ellipse({10.0f, 20.0f}, 4.0f, 4.0f, colors::White, 2.0f);

    REQUIRE(renderer.count() == 2);
    const auto& fill = std::get<FillEllipse>(renderer.commands[0]);
    REQUIRE(fill.center.x == 10.0f);
    REQUIRE(fill.radius_x == 5.0f);
    REQUIRE(fill.radius_y == 6.0f);
    const auto& stroke = std::get<StrokeEllipse>(renderer.commands[1]);
    REQUIRE(stroke.stroke_width == 2.0f);

    const std::string text = describe(renderer.commands);
    REQUIRE(text.find("FillEllipse") != std::string::npos);
    REQUIRE(text.find("StrokeEllipse") != std::string::npos);
}

TEST_CASE("PushClipEllipse round-trips through TestRenderer and describe(), reusing PopClip", "[render][clip]")
{
    TestRenderer renderer;
    renderer.push_clip_ellipse({10.0f, 20.0f}, 5.0f, 6.0f);
    renderer.pop_clip();

    REQUIRE(renderer.count() == 2);
    const auto& push = std::get<PushClipEllipse>(renderer.commands[0]);
    REQUIRE(push.center.x == 10.0f);
    REQUIRE(push.radius_x == 5.0f);
    REQUIRE(push.radius_y == 6.0f);
    REQUIRE(std::holds_alternative<PopClip>(renderer.commands[1]));

    const std::string text = describe(renderer.commands);
    REQUIRE(text.find("PushClipEllipse") != std::string::npos);
    REQUIRE(text.find("PopClip") != std::string::npos);
}

TEST_CASE("PushClipRoundedRect round-trips through TestRenderer, a recording and describe()", "[render][clip]")
{
    RenderRecording recording;
    {
        const ClipScope clip{recording, Rect{1.0f, 2.0f, 30.0f, 40.0f}, 6.0f};
    }
    TestRenderer renderer;
    recording.replay(renderer);

    REQUIRE(renderer.count() == 2);
    const auto& push = std::get<PushClipRoundedRect>(renderer.commands[0]);
    CHECK(push.bounds == Rect{1.0f, 2.0f, 30.0f, 40.0f});
    CHECK(push.radius == 6.0f);
    CHECK(std::holds_alternative<PopClip>(renderer.commands[1]));
    CHECK(describe(renderer.commands).find("PushClipRoundedRect rect=") != std::string::npos);
}

TEST_CASE("draw_image round-trips through TestRenderer and describe(), capturing only the image's size",
          "[render][image]")
{
    TestRenderer renderer;
    Bitmap       img(12, 8, colors::Black);
    renderer.draw_image(img, Rect{0, 0, 40, 20}, Stretch::UniformToFill);

    REQUIRE(renderer.count() == 1);
    const auto& cmd = std::get<DrawImage>(renderer.commands[0]);
    REQUIRE(cmd.image_size.width == 12.0f);
    REQUIRE(cmd.image_size.height == 8.0f);
    REQUIRE(cmd.stretch == Stretch::UniformToFill);

    const std::string text = describe(renderer.commands);
    REQUIRE(text.find("DrawImage") != std::string::npos);
    REQUIRE(text.find("stretch=uniform_to_fill") != std::string::npos);
}

TEST_CASE("FillRectPattern round-trips through TestRenderer and describe()", "[render][pattern]")
{
    TestRenderer renderer;
    renderer.fill_rect_pattern(Rect{0, 0, 8, 8}, Pattern::Checkerboard50, colors::Black, colors::White);

    REQUIRE(renderer.count() == 1);
    const auto& p = std::get<FillRectPattern>(renderer.commands[0]);
    REQUIRE(p.fg == colors::Black);
    REQUIRE(p.bg == colors::White);
    REQUIRE(describe(renderer.commands).find("FillRectPattern") != std::string::npos);
}

TEST_CASE("LinearGradientBrush defaults to Vertical so every existing gradient caller is unaffected",
          "[render][gradient]")
{
    REQUIRE(LinearGradientBrush{}.axis == GradientAxis::Vertical);

    TestRenderer        renderer;
    LinearGradientBrush brush{{GradientStop{0.0f, colors::Black}, GradientStop{1.0f, colors::White}},
                              GradientAxis::Horizontal};
    renderer.fill_rounded_rect_gradient(Rect{0, 0, 40, 20}, 0.0f, brush);
    const std::string text = describe(renderer.commands);
    REQUIRE(text.find("axis=horizontal") != std::string::npos);
}

// ---- Optional image theme backend ---------------------------------------------------------

TEST_CASE("ImageThemePack stores and finds images/metrics by name; empty() reflects that", "[render][image_theme]")
{
    ImageThemePack pack;
    REQUIRE(pack.empty());
    REQUIRE(pack.find_image("button") == nullptr);

    Bitmap img(4, 4, colors::White);
    pack.add("button", img, NineSliceMetrics{Thickness{2.0f}, Thickness{1.0f}});

    REQUIRE_FALSE(pack.empty());
    const Bitmap* found = pack.find_image("button");
    REQUIRE(found != nullptr);
    REQUIRE(found->width() == 4);
    const NineSliceMetrics* metrics = pack.find_metrics("button");
    REQUIRE(metrics != nullptr);
    REQUIRE(metrics->slice_margins.left == 2.0f);
    REQUIRE(pack.find_image("missing") == nullptr);
}

TEST_CASE("DrawImageNineSlice round-trips through TestRenderer and describe()", "[render][image_theme]")
{
    TestRenderer renderer;
    renderer.draw_image_nine_slice("button", Rect{0, 0, 80, 24}, Thickness{3.0f});

    REQUIRE(renderer.count() == 1);
    const auto& cmd = std::get<DrawImageNineSlice>(renderer.commands[0]);
    REQUIRE(cmd.image_name == "button");
    REQUIRE(cmd.slice_margins.left == 3.0f);
    REQUIRE(describe(renderer.commands).find("DrawImageNineSlice") != std::string::npos);
}

// ---- Disabled labels, and borders drawn inside ----------------------------------------------

namespace
{
// The label run of a toggle control or TextControl: the one DrawTextRun that carries `text`.
const DrawTextRun* label_run(const TestRenderer& renderer, const std::string& text)
{
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t && t->text == text)
        {
            return t;
        }
    }
    return nullptr;
}

template <class Control>
TestRenderer render_disabled(Control& control, const Theme& theme)
{
    HeadlessTextMeasurer measurer;
    control.set_enabled(false);
    const Size d = control.measure(Size{240.0f, Unbounded}, measurer, theme);
    control.arrange(Rect{0.0f, 0.0f, 240.0f, std::max(d.height, 24.0f)});
    TestRenderer renderer;
    control.render(renderer, theme);
    return renderer;
}
} // namespace

TEST_CASE("A disabled toggle's label is engraved under Windows 9x, and its glyph is not", "[render][retro][regression]")
{
    // Disabled labels engrave, disabled glyphs stay flat BTNSHADOW. Both classic sources agree.
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});

    CheckBoxControl check{"Label", /*checked=*/true};
    TestRenderer    r     = render_disabled(check, theme);
    const auto*     label = label_run(r, "Label");
    REQUIRE(label);
    CHECK(label->style.effect.kind == TextEffectKind::Emboss);
    CHECK(label->color == theme.colors().text_disabled);
    bool glyph_seen = false;
    for (const auto& cmd : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t && t->text != "Label")
        {
            CHECK(t->style.effect.kind == TextEffectKind::None); // the tick
            CHECK(t->color == ui::detail::controls(theme).check_box_box.disabled.on.text);
            glyph_seen = true;
        }
    }
    CHECK(glyph_seen);

    RadioButtonControl radio{"Label", /*checked=*/true};
    TestRenderer       rr = render_disabled(radio, theme);
    REQUIRE(label_run(rr, "Label"));
    CHECK(label_run(rr, "Label")->style.effect.kind == TextEffectKind::Emboss);

    ToggleSwitchControl toggle{/*on=*/true}; // labeled "On" by default
    TestRenderer        rt = render_disabled(toggle, theme);
    REQUIRE(label_run(rt, "On"));
    CHECK(label_run(rt, "On")->style.effect.kind == TextEffectKind::Emboss);

    TextControl  text{"Label"};
    TestRenderer rb = render_disabled(text, theme);
    REQUIRE(label_run(rb, "Label"));
    CHECK(label_run(rb, "Label")->style.effect.kind == TextEffectKind::Emboss);
    CHECK(label_run(rb, "Label")->color == theme.colors().text_disabled);
}

TEST_CASE("A disabled label grays without engraving outside the 9x lineage", "[render][theme]")
{
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Windows3x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});

        CheckBoxControl check{"Label"};
        TestRenderer    r = render_disabled(check, theme);
        REQUIRE(label_run(r, "Label"));
        CHECK(label_run(r, "Label")->color == theme.colors().text_disabled);
        CHECK(label_run(r, "Label")->style.effect.kind == TextEffectKind::None);

        // An explicit color does not survive disabling: a label that keeps its custom color does
        // not read as disabled, and `DrawState(DSS_DISABLED)` ignores the text color too.
        TextControl text{"Label"};
        text.set_color(Color::from_argb(0xFFFF0000));
        TestRenderer rb = render_disabled(text, theme);
        REQUIRE(label_run(rb, "Label"));
        CHECK(label_run(rb, "Label")->color == theme.colors().text_disabled);
    }
}

TEST_CASE("An enabled label keeps the page text color", "[render][theme]")
{
    const Theme          theme = Theme::fluent(Palette::fluent_light());
    HeadlessTextMeasurer measurer;
    CheckBoxControl      check{"Label"};
    const Size           d = check.measure(Size{240.0f, Unbounded}, measurer, theme);
    check.arrange(Rect{0.0f, 0.0f, 240.0f, d.height});
    TestRenderer r;
    check.render(r, theme);
    REQUIRE(label_run(r, "Label"));
    CHECK(label_run(r, "Label")->color == theme.colors().text);
}

TEST_CASE("Control borders are stroked inside their bounds, flat and gradient alike", "[render][chrome][regression]")
{
    // A stroke centered on a control's own rect puts half of itself outside it, which a clipping
    // parent cuts. So it draws inside.
    const Rect  bounds{10.0f, 20.0f, 100.0f, 32.0f};
    const Theme theme = Theme::fluent(Palette::fluent_light());

    TestRenderer flat;
    draw_control_chrome(flat,
                        bounds,
                        4.0f,
                        ui::detail::controls(theme).check_box_box.normal.off,
                        1.0f,
                        /*draw_flat_border_fallback=*/true);
    const auto* stroke = [&]() -> const StrokeRoundedRect*
    {
        for (const auto& cmd : flat.commands)
        {
            if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd))
            {
                return s;
            }
        }
        return nullptr;
    }();
    REQUIRE(stroke);
    CHECK(stroke->bounds == Rect{10.5f, 20.5f, 99.0f, 31.0f});
    CHECK(stroke->radius == 3.5f);

    TestRenderer lifted;
    draw_control_chrome(lifted, bounds, 4.0f, theme.button_normal, 1.0f);
    const auto* gradient = [&]() -> const StrokeRoundedRectGradient*
    {
        for (const auto& cmd : lifted.commands)
        {
            if (const auto* s = std::get_if<StrokeRoundedRectGradient>(&cmd))
            {
                return s;
            }
        }
        return nullptr;
    }();
    REQUIRE(gradient);
    CHECK(gradient->bounds == Rect{10.5f, 20.5f, 99.0f, 31.0f});
    CHECK(gradient->radius == 3.5f);
    CHECK(gradient->extent_px == theme.button_normal.border_gradient_extent_px);
}

TEST_CASE("Controls that stroke their own border keep it inside themselves, under every theme",
          "[render][chrome][regression]")
{
    // Fields, bars, cards and spin buttons stroke their borders directly, not through
    // draw_control_chrome, so nothing else would catch one going back to stroking on its bounds.
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

        std::vector<std::unique_ptr<Element>> controls;
        controls.push_back(std::make_unique<TextBoxControl>("Text"));
        controls.push_back(std::make_unique<InfoBarControl>("Title", "Message", InfoBarSeverity::Warning, true));
        controls.push_back(std::make_unique<NumberBoxControl>(3.0));
        auto combo = std::make_unique<ComboBoxControl>(std::vector<std::string>{"One", "Two"}, 0);
        combo->set_editable(true);
        controls.push_back(std::move(combo));
        controls.push_back(std::make_unique<ExpanderControl>("Header", /*expanded=*/false));

        for (const auto& control : controls)
        {
            const Size d = control->measure(Size{320.0f, Unbounded}, measurer, theme);
            const Rect bounds{4.0f, 6.0f, 320.0f, std::max(d.height, 32.0f)};
            control->arrange(bounds);
            TestRenderer renderer;
            control->render(renderer, theme);

            auto inside = [&](const Rect& path, float width)
            {
                const float half = width * 0.5f;
                CHECK(path.left() - half >= bounds.left() - 0.001f);
                CHECK(path.top() - half >= bounds.top() - 0.001f);
                CHECK(path.right() + half <= bounds.right() + 0.001f);
                CHECK(path.bottom() + half <= bounds.bottom() + 0.001f);
            };
            for (const auto& cmd : renderer.commands)
            {
                if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd))
                {
                    inside(s->bounds, s->stroke_width);
                }
                else if (const auto* g = std::get_if<StrokeRoundedRectGradient>(&cmd))
                {
                    inside(g->bounds, g->stroke_width);
                }
            }
        }
    }
}

// ---- The indeterminate check box and the radio dot's lift -----------------------------------

namespace
{
TestRenderer render_indeterminate(const Theme& theme, bool pressed = false)
{
    CheckBoxControl box{"", std::nullopt};
    box.arrange(Rect{0.0f, 0.0f, ui::detail::controls(theme).toggle_box_size, ui::detail::controls(theme).toggle_box_size});
    if (pressed)
    {
        box.handle_pointer({Point{1.0f, 1.0f}, PointerButton::Left, PointerAction::Down});
    }
    TestRenderer renderer;
    box.render(renderer, theme);
    return renderer;
}

std::vector<const DrawTextRun*> text_runs(const TestRenderer& renderer)
{
    std::vector<const DrawTextRun*> runs;
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            runs.push_back(t);
        }
    }
    return runs;
}
} // namespace

TEST_CASE("An indeterminate Fluent 2 check box is the checked box with the dash glyph", "[render][checkbox]")
{
    const Theme  theme = Theme::fluent(Palette::fluent_light());
    TestRenderer r     = render_indeterminate(theme);
    REQUIRE(std::holds_alternative<FillRoundedRect>(r.commands.front()));
    CHECK(std::get<FillRoundedRect>(r.commands.front()).color ==
          ui::detail::controls(theme).check_box_box.normal.on.background);
    const auto runs = text_runs(r);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0]->text == encode_utf8(0xE9AE));
    CHECK(runs[0]->color == ui::detail::controls(theme).check_box_box.normal.on.text);
    CHECK(r.count_of<FillRectPattern>() == 0);
}

TEST_CASE("A mixed Windows 9x check box dithers inside its well and grays its tick", "[render][checkbox][retro]")
{
    const Theme  theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    TestRenderer r     = render_indeterminate(theme);
    CHECK(r.count_of<StrokeLine>() == 8); // the sunken well is intact
    REQUIRE(r.count_of<FillRectPattern>() == 1);
    for (const auto& cmd : r.commands)
    {
        if (const auto* p = std::get_if<FillRectPattern>(&cmd))
        {
            const float size = ui::detail::controls(theme).toggle_box_size;
            CHECK(p->bounds == Rect{2.0f, 2.0f, size - 4.0f, size - 4.0f}); // inside both rings
            CHECK(p->fg == colors::White);                                  // BTNHIGHLIGHT
            CHECK(p->bg == ui::detail::controls(theme).check_box_indeterminate.normal.box.background); // BTNFACE
        }
    }
    const auto runs = text_runs(r);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0]->color == Color::from_argb(0xFF808080)); // BTNSHADOW

    // Pressed: the interior goes to plain face, as every pressed box does.
    CHECK(render_indeterminate(theme, /*pressed=*/true).count_of<FillRectPattern>() == 0);
}

TEST_CASE("A grayed Windows 1.x-3.x check box is a dithered square with no mark", "[render][checkbox][retro]")
{
    for (ThemeVariant v : {ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme  theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        TestRenderer r     = render_indeterminate(theme);
        CHECK(text_runs(r).empty()); // no X
        REQUIRE(r.count_of<FillRectPattern>() == 1);
        for (const auto& cmd : r.commands)
        {
            if (const auto* p = std::get_if<FillRectPattern>(&cmd))
            {
                const float size = ui::detail::controls(theme).toggle_box_size;
                CHECK(p->bounds == Rect{1.0f, 1.0f, size - 2.0f, size - 2.0f}); // inside the 1px outline
                CHECK(p->fg == ui::detail::controls(theme).check_box_box.normal.on.text);
                CHECK(p->bg == ui::detail::controls(theme).check_box_box.normal.on.background);
            }
        }
    }
}

TEST_CASE("Metro, Luna and Aero mark the mixed value with a centered square", "[render][checkbox][retro]")
{
    struct Case
    {
        ThemeVariant variant;
        float        side;
    };
    for (const Case& c : {Case{ThemeVariant::Metro, 9.0f}, Case{ThemeVariant::Luna, 7.0f}, Case{ThemeVariant::Aero, 9.0f}})
    {
        CAPTURE(static_cast<int>(c.variant));
        const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = c.variant}, OsThemeSnapshot{});
        TestRenderer r = render_indeterminate(theme);
        CHECK(text_runs(r).empty());
        const float inset = (ui::detail::controls(theme).toggle_box_size - c.side) * 0.5f;
        const Rect  square{inset, inset, c.side, c.side};
        bool        found = false;
        for (const auto& cmd : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&cmd); f && f->bounds == square)
            {
                CHECK(f->color == ui::detail::controls(theme).check_box_indeterminate.normal.box.text);
                found = true;
            }
            if (const auto* g = std::get_if<FillRoundedRectGradient>(&cmd); g && g->bounds == square)
            {
                found = true;
            }
        }
        CHECK(found);
    }

    // Aero's square has a darker inner border, stroked inside it, and a highlight ring inside that.
    const Theme       aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    TestRenderer      r    = render_indeterminate(aero);
    std::vector<Rect> gradient_strokes;
    for (const auto& cmd : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRectGradient>(&cmd))
        {
            gradient_strokes.push_back(s->bounds);
        }
    }
    CHECK(std::find(gradient_strokes.begin(), gradient_strokes.end(), Rect{2.5f, 2.5f, 8.0f, 8.0f}) !=
          gradient_strokes.end());
    CHECK(std::find(gradient_strokes.begin(), gradient_strokes.end(), Rect{3.5f, 3.5f, 6.0f, 6.0f}) !=
          gradient_strokes.end());
}

TEST_CASE("An Aero check box is a square recessed well: rim and inner fill under the border, then the tick",
          "[render][checkbox][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).toggle_box_size == 13.0f);

    const auto render_box = [&](bool checked, bool enabled)
    {
        CheckBoxControl box{"", checked};
        box.set_enabled(enabled);
        box.arrange(Rect{0.0f, 0.0f, 13.0f, 13.0f});
        TestRenderer renderer;
        box.render(renderer, theme);
        return renderer;
    };

    for (bool checked : {false, true})
    {
        CAPTURE(checked);
        TestRenderer r = render_box(checked, true);
        // The box at 13x13: the flat well (1, 1, 11, 11), the diagonal rim (2, 2, 9, 9), the
        // 7x7 diagonal inner fill at (3, 3), then the square-cornered border.
        std::vector<const FillRoundedRectGradient*> fills;
        for (const auto& cmd : r.commands)
        {
            if (const auto* g = std::get_if<FillRoundedRectGradient>(&cmd))
            {
                fills.push_back(g);
            }
        }
        REQUIRE(fills.size() == 2);
        CHECK(fills[0]->bounds == Rect{2.0f, 2.0f, 9.0f, 9.0f});
        CHECK(fills[0]->brush.axis == GradientAxis::Diagonal);
        CHECK(fills[0]->brush.stops.front().color == Color::from_argb(0xFFAEB3B9));
        CHECK(fills[1]->bounds == Rect{3.0f, 3.0f, 7.0f, 7.0f});
        CHECK(fills[1]->brush.stops.front().color == Color::from_argb(0xFFCBCFD5));
        REQUIRE(r.count_of<StrokeRoundedRect>() == 1);
        for (const auto& cmd : r.commands)
        {
            if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd))
            {
                CHECK(s->radius == 0.0f);
                CHECK(s->color == Color::from_argb(0xFF8E8F8F));
            }
        }
        CHECK(text_runs(r).size() == (checked ? 1u : 0u));
    }

    // Disabled keeps the rim and drops the fill, and the tick grays to `#AEB7CF`.
    TestRenderer disabled = render_box(true, false);
    CHECK(disabled.count_of<FillRoundedRectGradient>() == 1);
    const auto runs = text_runs(disabled);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0]->color == Color::from_argb(0xFFAEB7CF));
}

TEST_CASE("An Aero radio is a round recessed well, and its dot a radial bead in a dark ring", "[render][radio][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    const auto render_radio = [&](bool checked)
    {
        RadioButtonControl radio{"", checked};
        radio.arrange(Rect{0.0f, 0.0f, 13.0f, 13.0f});
        TestRenderer renderer;
        radio.render(renderer, theme);
        return renderer;
    };

    for (bool checked : {false, true})
    {
        CAPTURE(checked);
        TestRenderer r = render_radio(checked);
        // The rim over (2, 2, 9, 9) and the inner fill over (3, 3, 7, 7), both discs.
        std::vector<const FillRoundedRectGradient*> fills;
        for (const auto& cmd : r.commands)
        {
            if (const auto* g = std::get_if<FillRoundedRectGradient>(&cmd))
            {
                fills.push_back(g);
            }
        }
        REQUIRE(fills.size() == 2);
        CHECK(fills[0]->bounds == Rect{2.0f, 2.0f, 9.0f, 9.0f});
        CHECK(fills[0]->radius == 4.5f);
        CHECK(fills[1]->bounds == Rect{3.0f, 3.0f, 7.0f, 7.0f});
        CHECK(r.count_of<DrawGeometry>() == (checked ? 1 : 0));
    }

    // The bead: a 7px disc with its radial brush mapped to the 6px disc inside its 1px ring, and
    // no flat `text`-colored dot.
    TestRenderer r = render_radio(true);
    for (const auto& cmd : r.commands)
    {
        if (const auto* g = std::get_if<DrawGeometry>(&cmd))
        {
            CHECK(g->brush_box == Rect{3.5f, 3.5f, 6.0f, 6.0f});
            REQUIRE(g->fill.has_value());
            CHECK(std::holds_alternative<RadialGradientBrush>(*g->fill));
        }
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd))
        {
            CHECK(f->color != ui::detail::controls(theme).radio_ring.normal.on.text);
        }
    }
    bool ring = false;
    for (const auto& cmd : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd); s && s->color == Color::from_argb(0xFF193B55))
        {
            ring = true;
            CHECK(s->bounds == Rect{3.5f, 3.5f, 6.0f, 6.0f});
        }
    }
    CHECK(ring);
}

TEST_CASE("MDL2's indeterminate box is a hollow accent outline", "[render][checkbox][mdl2]")
{
    const Theme  theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    TestRenderer r     = render_indeterminate(theme);
    CHECK(std::get<FillRoundedRect>(r.commands.front()).color == colors::Transparent);
    REQUIRE(r.count_of<StrokeRoundedRect>() == 1);
    for (const auto& cmd : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd))
        {
            CHECK(s->color == ui::detail::controls(theme).check_box_box.normal.on.background);
        }
    }
    const auto runs = text_runs(r);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0]->text == encode_utf8(0xE73C));
}

TEST_CASE("A checked Fluent 2 radio strokes its dot with a bounding-box elevation gradient", "[render][radio]")
{
    const Theme          theme = Theme::fluent(Palette::fluent_light());
    HeadlessTextMeasurer measurer;
    RadioButtonControl   radio{"", true};
    const Size           d = radio.measure(Size{200.0f, Unbounded}, measurer, theme);
    radio.arrange(Rect{0.0f, 0.0f, 200.0f, d.height});
    TestRenderer r;
    radio.render(r, theme);

    const FillRoundedRect* dot = nullptr;
    for (const auto& cmd : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd))
        {
            dot = f; // the last fill is the dot
        }
    }
    REQUIRE(dot);
    CHECK(dot->bounds.width == ui::detail::controls(theme).radio_dot.rest);
    REQUIRE(r.count_of<StrokeRoundedRectGradient>() == 1);
    for (const auto& cmd : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRectGradient>(&cmd))
        {
            CHECK(s->bounds == dot->bounds.inset(Thickness{0.5f}));
            CHECK(s->extent_px == dot->bounds.height - 1.0f); // the whole path: RelativeToBoundingBox
            CHECK(s->stroke_width == 1.0f);
            CHECK(s->brush == *ui::detail::controls(theme).radio_ring.normal.on.mark_border_gradient);
        }
    }

    // Unchecked, and under Windows 10's Fluent, there is no dot stroke.
    RadioButtonControl off{"", false};
    off.arrange(Rect{0.0f, 0.0f, 200.0f, d.height});
    TestRenderer ro;
    off.render(ro, theme);
    CHECK(ro.count_of<StrokeRoundedRectGradient>() == 0);

    const Theme  fluent1 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    TestRenderer r1;
    radio.render(r1, fluent1);
    CHECK(r1.count_of<StrokeRoundedRectGradient>() == 0);
}

TEST_CASE("An inner highlight ring sits just inside the border, not over it", "[render][chrome][regression]")
{
    // Aero's 1px ring sits at (1.5, 1.5, w-3, h-3). Luna's 2px hot ring covers rows 1-2
    // inside its 1px border.
    const Rect  bounds{0.0f, 0.0f, 80.0f, 24.0f};
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    REQUIRE(aero.button_normal.inner_border);
    TestRenderer ra;
    draw_control_chrome(ra, bounds, 3.0f, aero.button_normal, 1.0f);
    const auto* ring = std::get_if<StrokeRoundedRectGradient>(&ra.commands.back());
    REQUIRE(ring);
    CHECK(ring->bounds == Rect{1.5f, 1.5f, 77.0f, 21.0f});
    CHECK(ring->extent_px == 21.0f);

    const Theme luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(luna).check_box_box.hover.off.inner_border);
    TestRenderer rl;
    draw_control_chrome(rl, Rect{0.0f, 0.0f, 13.0f, 13.0f}, 0.0f, ui::detail::controls(luna).check_box_box.hover.off, 1.0f, true);
    const auto* hot = std::get_if<StrokeRoundedRectGradient>(&rl.commands.back());
    REQUIRE(hot);
    CHECK(hot->bounds == Rect{2.0f, 2.0f, 9.0f, 9.0f});
    CHECK(hot->stroke_width == 2.0f);
}

TEST_CASE("Every push_transform overload lands in the backend's push_matrix", "[render][transform]")
{
    TestRenderer renderer;
    Renderer&    base = renderer;
    renderer.push_transform(2.0f, Point{1.0f, 2.0f}); // callable on the implementer, no using-declaration
    base.push_transform(Matrix{1.0f, 0.0f, 0.0f, 3.0f, 4.0f, 5.0f});
    base.push_transform(1.5f, 0.5f, Point{});
    REQUIRE(renderer.count() == 3);
    REQUIRE(std::get<PushTransform>(renderer.commands[0]).matrix() == Matrix{2.0f, 0.0f, 0.0f, 2.0f, 1.0f, 2.0f});
    REQUIRE(std::get<PushTransform>(renderer.commands[1]).matrix() == Matrix{1.0f, 0.0f, 0.0f, 3.0f, 4.0f, 5.0f});
    REQUIRE(std::get<PushTransform>(renderer.commands[2]).matrix() == Matrix{1.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f});
}

TEST_CASE("A default-constructed IconGlyph is fully defined", "[render][icon]")
{
    constexpr IconGlyph glyph{};
    STATIC_REQUIRE(glyph.codepoint == 0);
    REQUIRE(glyph == IconGlyph{});
    REQUIRE_FALSE(glyph == glyphs::ChevronDown);
}

TEST_CASE("An IconSource measures on a worker while copies of it draw", "[render][icon][thread]")
{
    const IconSource glyph  = IconGlyph{0xE72D, "Segoe Fluent Icons", 16.0f};
    const IconSource bitmap = IconSource::bitmap(Bitmap(8, 8, colors::White));
    const IconSource copy   = bitmap; // shares its tint cache with `bitmap`

    std::atomic<bool> mismatch{false};
    std::thread       worker(
        [&]
        {
            const HeadlessTextMeasurer measurer;
            const Size                 first = glyph.measure(measurer);
            TestRenderer               renderer;
            for (int i = 0; i < 2000; ++i)
            {
                if (!(glyph.measure(measurer) == first))
                {
                    mismatch = true;
                }
                copy.draw(renderer, Rect{0.0f, 0.0f, 8.0f, 8.0f}, i % 2 == 0 ? colors::Black : colors::White);
            }
        });
    TestRenderer renderer;
    for (int i = 0; i < 2000; ++i)
    {
        glyph.draw(renderer, Rect{0.0f, 0.0f, 16.0f, 16.0f}, colors::Black);
        bitmap.draw(renderer, Rect{0.0f, 0.0f, 8.0f, 8.0f}, i % 2 == 0 ? colors::White : colors::Black);
    }
    worker.join();
    REQUIRE_FALSE(mismatch);
    REQUIRE(renderer.count() >= 4000); // a glyph and an image each time
}

TEST_CASE("An IconGlyph keeps its own copy of a font name taken from a string", "[render][icon]")
{
    IconGlyph glyph{0xE734};
    {
        const std::string family = "Segoe MDL2 Assets";
        glyph.font_family        = family.c_str();
    }
    CHECK(glyph.font_family.view() == "Segoe MDL2 Assets");
    CHECK(glyph == IconGlyph{0xE734, std::string("Segoe MDL2 Assets")});
    static_assert(glyphs::Accept.font_family == FontName{"Segoe Fluent Icons"});

    testing::TestRenderer renderer;
    draw_icon(renderer, glyph, Rect{0, 0, 16, 16}, colors::Black);
    CHECK(std::get<DrawTextRun>(renderer.commands.front()).style.font_family == "Segoe MDL2 Assets");
}

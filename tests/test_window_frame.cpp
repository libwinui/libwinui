// SPDX-License-Identifier: MIT
#include "ui/WindowHost.hpp"
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/dialogs/Popup.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace ui;
using namespace ui::testing;

namespace
{

// A theme with a classic-sized era frame: a 4 DIP border, an 18 DIP caption and a 1 DIP rule.
Theme framed_theme()
{
    Theme                              theme = Theme::light();
    detail::ControlChrome::WindowFrame frame;
    frame.active_caption                      = LinearGradientBrush{{GradientStop{0.0f, Color::from_argb(0xFF000080)},
                                                                     GradientStop{1.0f, Color::from_argb(0xFF000080)}},
                                                                    GradientAxis::Horizontal};
    frame.inactive_caption                    = LinearGradientBrush{{GradientStop{0.0f, Color::from_argb(0xFF808080)},
                                                                     GradientStop{1.0f, Color::from_argb(0xFF808080)}},
                                                                    GradientAxis::Horizontal};
    frame.active_text                         = colors::White;
    frame.inactive_text                       = Color::from_argb(0xFFC0C0C0);
    frame.font_family                         = "MS Sans Serif";
    detail::edit_controls(theme).window_frame = frame;
    return theme;
}

// The client point at the center of a caption button, read from the hit regions.
Point find_region(const Window& window, HitRegion region, Size client)
{
    for (float y = 0.5f; y < 30.0f; y += 1.0f)
    {
        for (float x = client.width - 0.5f; x > client.width - 80.0f; x -= 1.0f)
        {
            if (window.hit_region_at({x, y}) == region)
            {
                // Walk to the middle of the run along x.
                float right = x;
                float left  = x;
                while (window.hit_region_at({left - 1.0f, y}) == region)
                {
                    left -= 1.0f;
                }
                return {(left + right) * 0.5f, y + 5.0f};
            }
        }
    }
    return {-1.0f, -1.0f};
}

} // namespace

TEST_CASE("Frame::Themed is the OS frame under a theme without one", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().set_style({.frame = Frame::Themed});
    CHECK(seams(ui.window()).effective_style().frame == Frame::Standard);
    CHECK(ui.host().style().frame == Frame::Standard);
    CHECK(seams(ui.window()).drawn_frame_border() == 0.0f);
}

TEST_CASE("Under a theme with a frame, the host runs the window borderless", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_style({.frame = Frame::Themed});
    CHECK(ui.window().style().frame == Frame::Themed); // what the app asked for
    CHECK(seams(ui.window()).effective_style().frame == Frame::Borderless);
    CHECK(ui.host().style().frame == Frame::Borderless);
    CHECK(seams(ui.window()).drawn_frame_border() == 4.0f);

    SECTION("a theme switch flips the frame the host runs, once")
    {
        const int applied = ui.host().style_applications;
        ui.window().pin_theme(Theme::light());
        CHECK(ui.host().style().frame == Frame::Standard);
        CHECK(ui.host().style_applications == applied + 1);
        CHECK(seams(ui.window()).drawn_frame_border() == 0.0f);

        ui.window().pin_theme(framed_theme());
        CHECK(ui.host().style().frame == Frame::Borderless);
        ui.window().pin_theme(framed_theme()); // same frame: nothing to re-send
        CHECK(ui.host().style_applications == applied + 2);
    }
}

TEST_CASE("Content taller than the window is clipped to the inside of the drawn frame", "[window-frame][render]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{.content = {ui::Button{.text = "Tall", .height = 400}}});
    ui.layout({400.0f, 300.0f});

    TestRenderer r;
    ui.window().render(r);
    // The content's clip opens before its button's text and closes after it.
    const Rect  inside{4.0f, 23.0f, 392.0f, 273.0f};
    std::size_t open = r.commands.size();
    std::size_t text = r.commands.size();
    for (std::size_t i = 0; i < r.commands.size(); ++i)
    {
        if (const auto* clip = std::get_if<PushClip>(&r.commands[i]); clip != nullptr && clip->bounds == inside)
        {
            open = i;
        }
        if (const auto* run = std::get_if<DrawTextRun>(&r.commands[i]); run != nullptr && run->text == "Tall")
        {
            text = i;
        }
    }
    REQUIRE(open < text);
    REQUIRE(text < r.commands.size());
    int depth = 0;
    for (std::size_t i = open; i <= text; ++i)
    {
        depth += std::holds_alternative<PushClip>(r.commands[i]) ? 1 : 0;
        depth -= std::holds_alternative<PopClip>(r.commands[i]) ? 1 : 0;
    }
    CHECK(depth >= 1); // still inside the frame's clip when the text draws
}

TEST_CASE("The content is laid out inside the drawn border and caption", "[window-frame]")
{
    HeadlessUI              ui;
    ui::Control<ui::Button> ok;
    ui.window().pin_theme(framed_theme());
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{.content = {ui::Button{.text = "OK", .control = ok}}});
    ui.layout({400.0f, 300.0f});

    // 4 border + 18 caption + 1 rule on top, 4 on the other sides.
    const Rect content = ui.window().content()->bounds();
    CHECK(content.x == 4.0f);
    CHECK(content.y == 23.0f);
    CHECK(content.width == 392.0f);
    CHECK(content.height == 273.0f);

    SECTION("maximized drops the border, full screen the whole frame")
    {
        ui.simulate_state(WindowState::Maximized, {400.0f, 300.0f});
        CHECK(ui.window().content()->bounds().x == 0.0f);
        CHECK(ui.window().content()->bounds().y == 19.0f);
        ui.simulate_state(WindowState::Fullscreen, {400.0f, 300.0f});
        CHECK(ui.window().content()->bounds() == Rect{0.0f, 0.0f, 400.0f, 300.0f});
    }
    SECTION("switching to a theme without a frame gives the content the whole client")
    {
        ui.window().pin_theme(Theme::light());
        ui.layout({400.0f, 300.0f});
        CHECK(ui.window().content()->bounds() == Rect{0.0f, 0.0f, 400.0f, 300.0f});
    }
}

TEST_CASE("The drawn caption drags, its buttons are caption buttons, the content is Client", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.layout(client);
    const Window& window = ui.window();

    CHECK(window.hit_region_at({100.0f, 10.0f}) == HitRegion::Caption);
    CHECK(window.hit_region_at({200.0f, 150.0f}) == HitRegion::Client);
    // Close: 2 DIP in from the caption's right edge, 16 wide, from 2 below its top.
    CHECK(window.hit_region_at({client.width - 4.0f - 2.0f - 8.0f, 4.0f + 2.0f + 7.0f}) == HitRegion::CloseButton);
    // Maximize: a 2 DIP gap left of close; minimize right against it.
    CHECK(window.hit_region_at({client.width - 6.0f - 16.0f - 2.0f - 8.0f, 13.0f}) == HitRegion::MaximizeButton);
    CHECK(window.hit_region_at({client.width - 6.0f - 16.0f - 2.0f - 16.0f - 8.0f, 13.0f}) == HitRegion::MinimizeButton);
    // No icon set: no system-menu box, the caption's left end drags.
    CHECK(window.hit_region_at({8.0f, 13.0f}) == HitRegion::Caption);

    SECTION("an icon gets the system-menu box")
    {
        ui.window().set_icon(WindowIcon{.glyph = IconGlyph{0xE80F}});
        ui.layout(client);
        CHECK(window.hit_region_at({4.0f + 2.0f + 8.0f, 13.0f}) == HitRegion::SystemMenu);
    }
    SECTION("a disabled maximize button neither drags nor offers Snap Layouts")
    {
        ui.window().set_style({.maximize_button = false, .frame = Frame::Themed});
        ui.layout(client);
        CHECK(window.hit_region_at({client.width - 6.0f - 16.0f - 2.0f - 8.0f, 13.0f}) == HitRegion::Client);
        CHECK(window.hit_region_at({client.width - 6.0f - 16.0f - 2.0f - 16.0f - 8.0f, 13.0f}) == HitRegion::MinimizeButton);
    }
    SECTION("no caption buttons: no buttons at all")
    {
        ui.window().set_style({.frame = Frame::Themed, .caption_buttons = false});
        ui.layout(client);
        CHECK(window.hit_region_at({client.width - 14.0f, 13.0f}) == HitRegion::Caption);
    }
}

TEST_CASE("Without an icon of its own, the drawn caption shows the one the host reports", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_title("Notepad");
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.layout(client);

    const auto icon_and_title = [&]
    {
        TestRenderer r;
        ui.window().render(r);
        std::pair<Rect, float> found{};
        for (const auto& c : r.commands)
        {
            if (const auto* i = std::get_if<DrawImage>(&c))
            {
                found.first = i->bounds;
            }
            if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == "Notepad")
            {
                found.second = t->bounds.x;
            }
        }
        return found;
    };

    // Nothing reported yet: no icon, and the title starts at the caption's left end.
    const auto [none, bare_title] = icon_and_title();
    CHECK(none.width == 0.0f);

    seams(ui.window()).report_caption_icon(Bitmap{16, 16, colors::White});
    ui.layout(client);
    const auto [icon, title] = icon_and_title();
    CHECK(icon.width > 0.0f);
    CHECK(title > icon.right());
    CHECK(title > bare_title);
    CHECK(ui.window().hit_region_at(icon.center()) == HitRegion::SystemMenu);

    SECTION("hiding the caption icon hides the reported one too")
    {
        ui.window().set_style({.frame = Frame::Themed, .caption_icon = false});
        ui.layout(client);
        CHECK(icon_and_title().first.width == 0.0f);
    }
}

TEST_CASE("Clicking the drawn buttons minimizes, maximizes, restores and closes", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.show(client);
    ui.layout(client);
    Window& window = ui.window();

    const auto click = [&](HitRegion region)
    {
        const Point p = find_region(window, region, client);
        REQUIRE(p.x >= 0.0f);
        seams(window).handle_pointer({p, PointerButton::None, PointerAction::Move});
        seams(window).handle_pointer({p, PointerButton::Left, PointerAction::Down});
        seams(window).handle_pointer({p, PointerButton::Left, PointerAction::Up});
        ui.layout(client);
    };

    click(HitRegion::MaximizeButton);
    CHECK(window.state().get() == WindowState::Maximized);
    click(HitRegion::MaximizeButton); // now the restore button
    CHECK(window.state().get() == WindowState::Normal);
    click(HitRegion::CloseButton);
    CHECK(ui.host().close_requests == 1);
    click(HitRegion::MinimizeButton);
    CHECK(window.state().get() == WindowState::Minimized);

    SECTION("a press dragged off the button and released does nothing")
    {
        ui.simulate_state(WindowState::Normal, client);
        const Point p = find_region(window, HitRegion::CloseButton, client);
        seams(window).handle_pointer({p, PointerButton::Left, PointerAction::Down});
        seams(window).handle_pointer({{100.0f, 10.0f}, PointerButton::None, PointerAction::Move});
        seams(window).handle_pointer({{100.0f, 10.0f}, PointerButton::Left, PointerAction::Up});
        CHECK(ui.host().close_requests == 1);
    }
}

TEST_CASE("The drawn caption is a TitleBar whose buttons UI Automation can invoke", "[window-frame][automation]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_title("Notepad");
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.show(client);
    ui.layout(client);
    Window& window = ui.window();

    Element* title_bar = seams(window).drawn_title_bar();
    REQUIRE(title_bar != nullptr);
    CHECK(describe_automation(*title_bar) ==
          "TitleBar \"Notepad\"\n"
          "  Button \"Minimize\" [Invoke]\n"
          "  Button \"Maximize\" [Invoke]\n"
          "  Button \"Close\" [Invoke]\n");
    // Each node sits where its button is drawn.
    const std::vector<Element*> buttons = automation_children(*title_bar);
    REQUIRE(buttons.size() == 3);
    CHECK(window.hit_region_at(buttons[2]->bounds().center()) == HitRegion::CloseButton);
    CHECK(title_bar->bounds().contains(buttons[0]->bounds().center()));

    CHECK(ui::automation_providers(*buttons[1]).invoke->invoke());
    CHECK(window.state().get() == WindowState::Maximized);
    ui.layout(client);
    CHECK(buttons[1]->automation_name() == "Restore");
    CHECK(ui::automation_providers(*buttons[2]).invoke->invoke());
    CHECK(ui.host().close_requests == 1);

    SECTION("a disabled button refuses, a hidden one is not listed")
    {
        ui.window().set_style({.maximize_button = false, .frame = Frame::Themed});
        ui.layout(client);
        CHECK_FALSE(ui::automation_providers(*automation_children(*title_bar)[1]).invoke->invoke());
        ui.window().set_style({.frame = Frame::Themed, .caption_buttons = false});
        CHECK(automation_children(*title_bar).empty());
    }
    SECTION("no drawn frame, no title bar node")
    {
        ui.window().pin_theme(Theme::light());
        CHECK(seams(window).drawn_title_bar() == nullptr);
    }
}

TEST_CASE("The frame draws the caption in the active or the inactive colors", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_title("Notepad");
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.layout(client);

    const auto caption_and_title = [&]
    {
        TestRenderer r;
        ui.window().render(r);
        std::pair<Color, Color> found{};
        for (const auto& c : r.commands)
        {
            if (const auto* g = std::get_if<FillRoundedRectGradient>(&c))
            {
                found.first = g->brush.stops.front().color;
            }
            if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == "Notepad")
            {
                found.second = t->color;
            }
        }
        return found;
    };

    seams(ui.window()).report_active(true);
    CHECK(caption_and_title() == std::pair{Color::from_argb(0xFF000080), colors::White});
    seams(ui.window()).report_active(false);
    CHECK(caption_and_title() == std::pair{Color::from_argb(0xFF808080), Color::from_argb(0xFFC0C0C0)});
}

TEST_CASE("The classic schemes' captions are the ones the OS ships", "[window-frame][classic]")
{
    // Windows 95 and 98: derived from the selection and the bevel, and equal to the scheme data.
    const Theme standard = Theme::classic(ClassicPalette::windows_standard(), false, "MS Sans Serif");
    const auto& frame    = detail::controls(standard).window_frame;
    REQUIRE(frame);
    CHECK(frame->active_caption.stops.front().color == Color::from_argb(0xFF000080));
    CHECK(frame->active_caption.stops.back().color == Color::from_argb(0xFF000080)); // no gradient
    CHECK(frame->inactive_caption.stops.front().color == Color::from_argb(0xFF808080));
    CHECK(frame->inactive_text == Color::from_argb(0xFFC0C0C0));
    CHECK(frame->active_border == Color::from_argb(0xFFC0C0C0));
    CHECK(frame->caption_height == 18.0f);
    CHECK(frame->button == Size{16.0f, 14.0f});

    // Windows 2000's gradient.
    const Theme w2k_theme = Theme::classic(ClassicPalette::windows_standard_2000(), true, "Tahoma");
    const auto& w2k       = detail::controls(w2k_theme).window_frame;
    REQUIRE(w2k);
    CHECK(w2k->active_caption.stops.back().color == Color::from_argb(0xFFA6CAF0));
    CHECK(w2k->inactive_caption.stops.back().color == Color::from_argb(0xFFC0C0C0));

    // High Contrast Black: purple and green captions, a yellow frame and 20 px caption buttons.
    const Theme hc_theme = Theme::classic(ClassicPalette::high_contrast_black_2000(), false, "MS Sans Serif");
    const auto& hc       = detail::controls(hc_theme).window_frame;
    REQUIRE(hc);
    CHECK(hc->active_caption.stops.front().color == Color::from_argb(0xFF800080));
    CHECK(hc->inactive_caption.stops.front().color == Color::from_argb(0xFF008000));
    CHECK(hc->active_border == Color::from_argb(0xFFFFFF00));
    CHECK(hc->caption_height == 20.0f);
    CHECK(hc->button == Size{18.0f, 16.0f});

    // Windows 3.x: gray inactive captions with black titles, as most of its shipped schemes have.
    const Theme win3x = Theme::classic(ClassicPalette::windows_standard(), false, "System", true);
    const auto& w3    = detail::controls(win3x).window_frame;
    REQUIRE(w3);
    CHECK(w3->kind == detail::ControlChrome::WindowFrame::Kind::Win3x);
    CHECK(w3->inactive_caption.stops.front().color == Color::from_argb(0xFFC0C0C0));
    CHECK(w3->inactive_text == colors::Black);
    CHECK(w3->border == 5.0f);
}

TEST_CASE("An inactive early-Windows title reads against its caption", "[window-frame]")
{
    // Windows 2.x: a white inactive caption, so the title is the window text.
    const Theme two   = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});
    const auto& frame = *detail::controls(two).window_frame;
    CHECK(frame.inactive_caption.stops.front().color == colors::White);
    CHECK(frame.inactive_text == colors::Black);
    CHECK(frame.active_text == colors::White);

    // Windows 1.x: the darker inactive caption keeps the white title text.
    const Theme one = resolve_theme(ThemeRequest{.variant      = ThemeVariant::Windows1x,
                                                 .early_scheme = EarlyPalette::windows_1x_color()},
                                    OsThemeSnapshot{});
    CHECK(detail::controls(one).window_frame->inactive_text == colors::White);
}

TEST_CASE("Windows 8's frame is the window color, pale while inactive", "[window-frame]")
{
    const Color blue     = Color::from_argb(0xFF60B0F5);
    const auto  frame_of = [](const ThemeRequest& request)
    { return *detail::controls(resolve_theme(request, OsThemeSnapshot{})).window_frame; };

    const auto frame = frame_of({.variant = ThemeVariant::Metro});
    CHECK(frame.kind == detail::ControlChrome::WindowFrame::Kind::Metro);
    CHECK(frame.active_caption.stops.back().color == blue);
    CHECK(frame.title_centered);
    // Inactive, a fifth of the color over white.
    const Color pale = frame.inactive_caption.stops.back().color;
    CHECK(pale.b > blue.b);
    CHECK(pale.r > 0.85f);

    // An app's accent becomes the window color.
    const Color green = Color::from_argb(0xFF008A00);
    CHECK(frame_of({.accent_override = green, .variant = ThemeVariant::Metro}).active_caption.stops.back().color == green);
}

TEST_CASE("Windows 8's caption buttons hang from the top edge, minimize narrower than maximize", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(resolve_theme({.variant = ThemeVariant::Metro}, OsThemeSnapshot{}));
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.layout(client);
    const Window& window = ui.window();

    // Close: 47 wide, 7 in from the right, rows 1 to 20.
    CHECK(window.hit_region_at({client.width - 7.0f - 1.0f, 1.5f}) == HitRegion::CloseButton);
    CHECK(window.hit_region_at({client.width - 7.0f - 46.5f, 20.5f}) == HitRegion::CloseButton);
    CHECK(window.hit_region_at({client.width - 7.0f - 47.0f - 1.0f, 10.0f}) == HitRegion::Caption); // the 2 px gap
    // Maximize 30 wide, then a 3 px gap, then minimize 28 wide.
    const float max_right = client.width - 7.0f - 47.0f - 2.0f;
    CHECK(window.hit_region_at({max_right - 29.5f, 10.0f}) == HitRegion::MaximizeButton);
    CHECK(window.hit_region_at({max_right - 31.5f, 10.0f}) == HitRegion::Caption);
    CHECK(window.hit_region_at({max_right - 33.5f, 10.0f}) == HitRegion::MinimizeButton);
    CHECK(window.hit_region_at({max_right - 33.0f - 27.5f, 10.0f}) == HitRegion::MinimizeButton);
    // The content starts 31 px down, inside the 8 px frame.
    CHECK(window.content()->bounds().y == 31.0f);
    CHECK(window.content()->bounds().x == 8.0f);
}

TEST_CASE("Aero's frame asks for the desktop blurred and paints glass once it is", "[window-frame][glass]")
{
    HeadlessUI ui;
    ui.window().pin_theme(resolve_theme({.variant = ThemeVariant::Aero}, OsThemeSnapshot{}));
    ui.window().set_style({.frame = Frame::Themed});
    ui.window().set_content(ui::VerticalStack{});
    const Size client{400.0f, 300.0f};
    ui.layout(client);
    Window&     window = ui.window();
    const auto& glass  = *detail::controls(window.theme()).window_frame->glass;

    CHECK(seams(window).wants_blur_behind());
    CHECK_FALSE(seams(window).blur_behind());

    const auto first_fill = [&]
    {
        TestRenderer r;
        window.render(r);
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c))
            {
                return f->color;
            }
            if (std::holds_alternative<FillRoundedRectGradient>(c))
            {
                return Color{};
            }
        }
        return Color{};
    };

    SECTION("until the host blurs, the frame is the opaque Basic art")
    {
        CHECK(first_fill() != glass.active_tint);
    }
    SECTION("over the blur it starts with the tint, and the content area is opaque")
    {
        seams(window).report_blur_behind(true);
        ui.layout(client);
        CHECK(first_fill() == glass.active_tint);

        TestRenderer r;
        window.render(r);
        const Rect content = window.content()->bounds();
        bool       filled  = false;
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->bounds == content)
            {
                filled = f->color == window.background();
            }
        }
        CHECK(filled);

        seams(window).report_active(false);
        CHECK(first_fill() == glass.inactive_tint);
    }
    SECTION("not with transparency effects off, full screen, or a frame without glass")
    {
        OsThemeSnapshot os;
        os.transparency_enabled = false;
        seams(window).report_os_theme(os);
        CHECK_FALSE(seams(window).wants_blur_behind());
        seams(window).report_os_theme(OsThemeSnapshot{});
        CHECK(seams(window).wants_blur_behind());

        ui.simulate_state(WindowState::Fullscreen, client);
        CHECK_FALSE(seams(window).wants_blur_behind());
        ui.simulate_state(WindowState::Normal, client);

        window.pin_theme(resolve_theme({.variant = ThemeVariant::Luna}, OsThemeSnapshot{}));
        CHECK_FALSE(seams(window).wants_blur_behind());
    }
}

TEST_CASE("Luna's frame rounds its top corners, except maximized or full screen", "[window-frame]")
{
    HeadlessUI ui;
    ui.window().pin_theme(resolve_theme({.variant = ThemeVariant::Luna}, OsThemeSnapshot{}));
    ui.window().set_style({.frame = Frame::Themed});
    const Size client{400.0f, 300.0f};
    ui.layout(client);
    Window&                         window = ui.window();
    const std::vector<std::uint8_t> luna{5, 3, 2, 1, 1};

    const auto cut = [&]
    {
        return std::vector<std::uint8_t>(seams(window).frame_corner_cut().begin(), seams(window).frame_corner_cut().end());
    };
    CHECK(cut() == luna);

    ui.simulate_state(WindowState::Maximized, client);
    CHECK(cut().empty());
    ui.simulate_state(WindowState::Fullscreen, client);
    CHECK(cut().empty());
    ui.simulate_state(WindowState::Normal, client);
    CHECK(cut() == luna);

    SECTION("not under a square frame, or the OS frame")
    {
        window.pin_theme(resolve_theme({.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{}));
        CHECK(cut().empty());
        window.pin_theme(resolve_theme({.variant = ThemeVariant::Luna}, OsThemeSnapshot{}));
        window.set_style({.frame = Frame::Standard});
        CHECK(cut().empty());
    }
}

TEST_CASE("Each rounded corner's art is transparent exactly where the window is cut", "[window-frame]")
{
    using CornerArt = detail::ControlChrome::WindowFrame::CornerArt;
    for (const ThemeVariant variant : {ThemeVariant::Luna, ThemeVariant::Aero})
    {
        const Theme theme = resolve_theme({.variant = variant}, OsThemeSnapshot{});
        const auto& frame = *detail::controls(theme).window_frame;
        REQUIRE_FALSE(frame.top_corner_cut.empty());
        const auto check = [&](const CornerArt& art, bool right)
        {
            REQUIRE(art.width > 0);
            const auto width = static_cast<std::size_t>(art.width);
            REQUIRE(art.pixels.size() == width * frame.top_corner_cut.size());
            for (std::size_t y = 0; y < frame.top_corner_cut.size(); ++y)
            {
                for (std::size_t x = 0; x < width; ++x)
                {
                    const std::size_t from_edge = right ? width - 1 - x : x;
                    CHECK((art.pixels[y * width + x].a == 0.0f) == (from_edge < frame.top_corner_cut[y]));
                }
            }
        };
        check(frame.active_top_left, false);
        check(frame.inactive_top_left, false);
        check(frame.active_top_right, true);
        check(frame.inactive_top_right, true);
    }
}

TEST_CASE("Rounded corners paint the art's pixels and nothing where the window is cut", "[window-frame]")
{
    // Whether any fill covers the pixel at (x, y).
    const auto painted = [](const TestRenderer& r, float x, float y)
    {
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->bounds.contains({x + 0.5f, y + 0.5f}))
            {
                return true;
            }
        }
        return false;
    };
    const auto color_at = [](const TestRenderer& r, float x, float y)
    {
        Color last{};
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->bounds.contains({x + 0.5f, y + 0.5f}))
            {
                last = f->color;
            }
        }
        return last;
    };

    HeadlessUI ui;
    ui.window().set_style({.frame = Frame::Themed});
    const Size client{400.0f, 300.0f};

    SECTION("Luna over its caption")
    {
        ui.window().pin_theme(resolve_theme({.variant = ThemeVariant::Luna}, OsThemeSnapshot{}));
        ui.layout(client);
        TestRenderer r;
        ui.window().render(r);
        // Row 0 starts 5 px in with the art's #004BCD, row 3 one pixel in with #0041CA.
        CHECK(color_at(r, 5.0f, 0.0f) == Color::from_argb(0xFF004BCD));
        CHECK(color_at(r, 1.0f, 3.0f) == Color::from_argb(0xFF0041CA));
    }
    SECTION("Aero glass, which has no region to hide what it paints")
    {
        ui.window().pin_theme(resolve_theme({.variant = ThemeVariant::Aero}, OsThemeSnapshot{}));
        ui.layout(client);
        CHECK(std::vector<std::uint8_t>(seams(ui.window()).frame_corner_cut().begin(),
                                        seams(ui.window()).frame_corner_cut().end()) ==
              std::vector<std::uint8_t>{4, 2, 1, 1});
        seams(ui.window()).report_blur_behind(true);
        CHECK(seams(ui.window()).frame_corner_cut().empty());
        ui.layout(client);
        TestRenderer r;
        ui.window().render(r);
        const std::vector<float> cut{4.0f, 2.0f, 1.0f, 1.0f};
        for (std::size_t y = 0; y < cut.size(); ++y)
        {
            for (float x = 0.0f; x < cut[y]; x += 1.0f)
            {
                CHECK_FALSE(painted(r, x, static_cast<float>(y)));
                CHECK_FALSE(painted(r, client.width - 1.0f - x, static_cast<float>(y)));
            }
            CHECK(painted(r, cut[y], static_cast<float>(y))); // the outline starts right at the step
        }
    }
}

TEST_CASE("Windows 1.x to 3.x captions have a Control-menu box and no close box", "[window-frame]")
{
    const auto check = [](const Theme& theme, bool has_buttons)
    {
        HeadlessUI ui;
        ui.window().pin_theme(theme);
        ui.window().set_style({.frame = Frame::Themed});
        ui.window().set_content(ui::VerticalStack{});
        const Size client{400.0f, 300.0f};
        ui.layout(client);
        const auto& frame = *detail::controls(theme).window_frame;
        const float b     = frame.border;
        const float mid   = b + frame.caption_height * 0.5f;
        // The box at the left, whether or not the window has an icon.
        CHECK(ui.window().hit_region_at({b + 4.0f, mid}) == HitRegion::SystemMenu);
        // The rightmost box maximizes, and nothing closes.
        CHECK(ui.window().hit_region_at({client.width - b - 4.0f, mid}) ==
              (has_buttons ? HitRegion::MaximizeButton : HitRegion::Caption));
        CHECK(ui.window().hit_region_at({client.width - b - frame.button.width - 4.0f, mid}) ==
              (has_buttons ? HitRegion::MinimizeButton : HitRegion::Caption));
        // A window without a maximize button moves minimize into its place.
        ui.window().set_style({.maximize_button = false, .frame = Frame::Themed});
        ui.layout(client);
        CHECK(ui.window().hit_region_at({client.width - b - 4.0f, mid}) ==
              (has_buttons ? HitRegion::MinimizeButton : HitRegion::Caption));
    };
    check(resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{}), true);
    check(resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{}), true);
    check(resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x, .early_scheme = EarlyPalette::windows_1x_color()},
                        OsThemeSnapshot{}),
          false);
}

namespace
{
// Asks for a popup above itself, the way a tool tip or an upward flyout does.
class AbovePopupOwner final : public Element
{
public:
    void show()
    {
        open_popup({.content = &tip_, .placement = PopupPlacement::Above, .light_dismiss = false});
    }
    [[nodiscard]] const TextControl& tip() const
    {
        return tip_;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {100.0f, 20.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    TextControl tip_{"tip"};
};
} // namespace

TEST_CASE("Popups inside a drawn frame stay off its caption", "[window-frame][popup]")
{
    HeadlessUI ui;
    ui.window().pin_theme(framed_theme());
    ui.window().set_style({.frame = Frame::Themed});

    SECTION("a popup placed above an anchor at the top of the content opens below it")
    {
        auto             canvas = std::make_unique<CanvasControl>();
        AbovePopupOwner* owner  = canvas->add(std::make_unique<AbovePopupOwner>(), 10.0f, 5.0f);
        ui.window().set_content(std::move(canvas));
        ui.layout({400.0f, 300.0f});
        owner->show();
        ui.layout({400.0f, 300.0f});
        CHECK(owner->tip().bounds().top() >= owner->bounds().bottom()); // 23 is the content's top
    }

    SECTION("a popup left over the caption is cut off there, and the caption keeps its presses")
    {
        ui.window().set_content(ui::Popup{.is_open         = true,
                                          .vertical_offset = -100.0f,
                                          .light_dismiss   = false,
                                          .content         = ui::Border{.width = 300.0f, .height = 150.0f}});
        ui.layout({400.0f, 300.0f});
        CHECK(ui.window().hit_region_at(Point{200.0f, 10.0f}) == HitRegion::Caption);

        TestRenderer r;
        ui.window().render(r);
        const Rect inside{4.0f, 23.0f, 392.0f, 273.0f};
        const auto clips = std::ranges::count_if(r.commands,
                                                 [&](const RenderCommand& c)
                                                 {
                                                     const auto* clip = std::get_if<PushClip>(&c);
                                                     return clip != nullptr && clip->bounds == inside;
                                                 });
        CHECK(clips == 2); // the content, then the popup
    }
}

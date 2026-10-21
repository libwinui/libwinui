// SPDX-License-Identifier: MIT
#include "ui/WindowHost.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/dialogs/Dialog.hpp"
#include "winui/ui/hosting/DragArea.hpp"
#include "winui/ui/hosting/TitleBar.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
void click(Window& window, Point p)
{
    ui::testing::seams(window).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(window).handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

// ---- state ----

TEST_CASE("A window starts Normal, and without a host a request goes nowhere", "[window-state]")
{
    Window window;
    REQUIRE(window.state().get() == WindowState::Normal);
    window.request_state(WindowState::Maximized);
    window.toggle_fullscreen();
    window.restore();
    CHECK(window.state().get() == WindowState::Normal);
    CHECK_FALSE(window.placement().has_value());
}

TEST_CASE("A size set before there is an OS window is what it is created with", "[window-state]")
{
    Window window{{.size = {800, 600}}};
    window.resize({500.0f, 300.0f});
    CHECK(window.options().size == Size{500.0f, 300.0f});

    // A size that is no size is ignored.
    window.resize({-1.0f, 300.0f});
    window.resize({std::numeric_limits<float>::quiet_NaN(), 10.0f});
    window.resize({std::numeric_limits<float>::infinity(), 10.0f});
    CHECK(window.options().size == Size{500.0f, 300.0f});
}

TEST_CASE("state() changes only when the host reports, never when the app asks", "[window-state]")
{
    HeadlessUI         ui;
    Window&            window   = ui.window();
    int                notified = 0;
    const Subscription watch    = window.state().observe([&](const WindowState&) { ++notified; });

    ui.host().defer_requests = true;
    window.request_state(WindowState::Maximized);
    CHECK(window.state().get() == WindowState::Normal); // asked, not yet answered
    CHECK(notified == 0);
    CHECK(ui.host().pending_request_count() == 1);

    ui.host().apply_pending_requests();
    CHECK(window.state().get() == WindowState::Maximized);
    CHECK(notified == 1);

    // A repeated report (WM_SIZE for a size that changed nothing) disturbs no one.
    ui::testing::seams(window).report_state(WindowState::Maximized);
    CHECK(notified == 1);
}

TEST_CASE("The user changing the state from the caption is reported like any other", "[window-state]")
{
    HeadlessUI ui;
    ui.show({800.0f, 600.0f});
    ui.simulate_state(WindowState::Maximized, {1920.0f, 1040.0f});
    CHECK(ui.window().state().get() == WindowState::Maximized);
    CHECK(ui.window().layout_size() == Size{1920.0f, 1040.0f});
}

TEST_CASE("restore() leaves Fullscreen for the state it was entered from", "[window-state]")
{
    HeadlessUI ui;
    Window&    window = ui.window();

    SECTION("entered from Normal")
    {
        window.request_state(WindowState::Fullscreen);
        REQUIRE(window.state().get() == WindowState::Fullscreen);
        window.restore();
        CHECK(window.state().get() == WindowState::Normal);
    }
    SECTION("entered from Maximized")
    {
        window.request_state(WindowState::Maximized);
        window.request_state(WindowState::Fullscreen);
        CHECK(window.placement()->restore_to_maximized);
        window.restore();
        CHECK(window.state().get() == WindowState::Maximized);
        window.restore();
        CHECK(window.state().get() == WindowState::Normal);
        window.restore(); // nothing further to restore to
        CHECK(window.state().get() == WindowState::Normal);
    }
}

TEST_CASE("Minimizing from full screen keeps full screen as the way back", "[window-state]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    window.request_state(WindowState::Maximized);
    window.request_state(WindowState::Fullscreen);
    ui.host().simulate_state(WindowState::Minimized); // the taskbar button

    window.restore();
    CHECK(window.state().get() == WindowState::Fullscreen);
    window.restore();
    CHECK(window.state().get() == WindowState::Maximized);
}

TEST_CASE("restores_to() says where restore() lands, before it is called", "[window-state]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    CHECK(window.restores_to().get() == WindowState::Normal);

    window.request_state(WindowState::Maximized);
    CHECK(window.restores_to().get() == WindowState::Normal);
    window.request_state(WindowState::Fullscreen);
    CHECK(window.restores_to().get() == WindowState::Maximized);
    ui.host().simulate_state(WindowState::Minimized);
    CHECK(window.restores_to().get() == WindowState::Fullscreen);

    // Each restore lands where restores_to() said it would.
    for (const WindowState expected : {WindowState::Fullscreen, WindowState::Maximized, WindowState::Normal})
    {
        CHECK(window.restores_to().get() == expected);
        window.restore();
        CHECK(window.state().get() == expected);
    }
    CHECK(window.restores_to().get() == WindowState::Normal);
}

TEST_CASE("restores_to() is current when a binding on state() runs", "[window-state]")
{
    HeadlessUI               ui;
    Window&                  window = ui.window();
    std::vector<WindowState> seen;
    const Subscription sub = window.state().observe([&](WindowState) { seen.push_back(window.restores_to().get()); });
    window.request_state(WindowState::Maximized);
    ui.host().simulate_state(WindowState::Minimized);
    CHECK(seen == std::vector<WindowState>{WindowState::Normal, WindowState::Maximized});
}

TEST_CASE("A host that reports only the state still gets restores_to() worked out", "[window-state]")
{
    struct GrantingHost final : WindowHost
    {
        Window& window;
        explicit GrantingHost(Window& w) : window(w)
        {
        }
        void request_state(WindowState s) override
        {
            testing::seams(window).report_state(s);
        }
    };
    Window       window;
    GrantingHost host{window};
    testing::seams(window).set_window_host(&host);

    window.request_state(WindowState::Maximized);
    window.request_state(WindowState::Minimized);
    CHECK(window.restores_to().get() == WindowState::Maximized);
    window.request_state(WindowState::Fullscreen); // from Minimized: the way back is still Maximized
    CHECK(window.restores_to().get() == WindowState::Maximized);
    window.request_state(WindowState::Minimized);
    CHECK(window.restores_to().get() == WindowState::Fullscreen);
    window.request_state(WindowState::Normal);
    CHECK(window.restores_to().get() == WindowState::Normal);
    testing::seams(window).set_window_host(nullptr);
}

TEST_CASE("toggle_fullscreen goes in and back out to where it started", "[window-state]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    window.request_state(WindowState::Maximized);
    window.toggle_fullscreen();
    CHECK(window.state().get() == WindowState::Fullscreen);
    window.toggle_fullscreen();
    CHECK(window.state().get() == WindowState::Maximized);
}

TEST_CASE("show() starts the window in its initial state", "[window-state]")
{
    HeadlessUI ui;
    ui.show({800.0f, 600.0f}, WindowState::Maximized);
    CHECK(ui.window().state().get() == WindowState::Maximized);

    HeadlessUI minimized;
    minimized.show({800.0f, 600.0f}, WindowState::Minimized);
    CHECK(minimized.window().state().get() == WindowState::Minimized);
    CHECK(minimized.window().layout_pass_count() == 0); // shown iconic: nothing laid out yet
}

TEST_CASE("While minimized, layout does nothing, and resumes on restore", "[window-state]")
{
    HeadlessUI            ui;
    ui::Control<ui::Text> label;
    ui.window().set_content(ui::VerticalStack{.content = {ui::Text{.text = "Hello", .control = label}}});
    ui.show({400.0f, 300.0f});
    const std::size_t passes = ui.window().layout_pass_count();

    // The 0x0 a real host reports for an iconic window is not a size to arrange the tree at.
    ui.simulate_state(WindowState::Minimized, {0.0f, 0.0f});
    CHECK(ui.window().layout_pass_count() == passes);
    CHECK(ui.window().layout_size() == Size{400.0f, 300.0f});

    label->set_text("Changed while minimized");
    ui.layout({0.0f, 0.0f});
    CHECK(ui.window().layout_pass_count() == passes);

    ui.simulate_state(WindowState::Normal, {400.0f, 300.0f});
    CHECK(ui.window().layout_pass_count() == passes + 1); // one pass, for the one real change
}

TEST_CASE("A full-screen toggle button, bound to state(), works end to end with no HWND", "[window-state]")
{
    HeadlessUI              ui;
    Window&                 window = ui.window();
    ui::Control<ui::Button> toggle;
    window.set_content(ui::Button{
        .text     = bind(window.state(),
                         [](WindowState s)
                         { return std::string{s == WindowState::Fullscreen ? "Exit full screen" : "Full screen"}; }),
        .on_click = [&window] { window.toggle_fullscreen(); },
        .control  = toggle,
    });
    ui.layout({800.0f, 600.0f});
    REQUIRE(toggle->text() == "Full screen");

    click(window, toggle->bounds().center());
    CHECK(window.state().get() == WindowState::Fullscreen);
    CHECK(toggle->text() == "Exit full screen");

    ui.layout({1920.0f, 1080.0f});
    click(window, toggle->bounds().center());
    CHECK(window.state().get() == WindowState::Normal);
    CHECK(toggle->text() == "Full screen");
}

TEST_CASE("placement() reports the normal bounds and the way back", "[window-state]")
{
    HeadlessUI ui;
    ui.host().normal_bounds = Rect{100.0f, 80.0f, 640.0f, 480.0f};
    ui.window().request_state(WindowState::Maximized);
    ui.host().simulate_state(WindowState::Minimized);

    const std::optional<WindowPlacement> placement = ui.window().placement();
    REQUIRE(placement.has_value());
    CHECK(placement->normal_bounds == Rect{100.0f, 80.0f, 640.0f, 480.0f});
    CHECK(placement->state == WindowState::Minimized);
    CHECK(placement->restore_to_maximized);
}

// ---- frame & chrome ----

TEST_CASE("FrameColor says follow, system default, none, or a color", "[chrome]")
{
    FrameColor follow;
    CHECK(follow.kind() == FrameColorKind::Follow);
    CHECK_FALSE(follow.has_value());

    FrameColor navy = Color::from_argb(0xFF000080);
    REQUIRE(navy.has_value());
    CHECK(*navy == Color::from_argb(0xFF000080));
    CHECK(navy == Color::from_argb(0xFF000080));
    navy.reset();
    CHECK(navy == FrameColor::follow());

    CHECK(FrameColor::none().kind() == FrameColorKind::None);
    CHECK(FrameColor::system_default().kind() == FrameColorKind::SystemDefault);
}

TEST_CASE("Chrome overrides win over the theme; what they leave unset follows it", "[chrome]")
{
    ChromeHints theme;
    theme.corner_preference = CornerPreference::Round;
    theme.dark_titlebar     = true;
    theme.caption_color     = Color::from_argb(0xFF000080);
    theme.border_color      = Color::from_argb(0xFF808080);

    CHECK(merge_chrome(theme, {}) == theme);

    // Square corners on a rounded theme, and no border line at all.
    const ChromeHints merged = merge_chrome(theme,
                                            {.corner_preference = CornerPreference::DoNotRound,
                                             .border_color      = FrameColor::none()});
    CHECK(merged.corner_preference == CornerPreference::DoNotRound);
    CHECK(merged.border_color == FrameColor::none());
    CHECK(merged.caption_color == Color::from_argb(0xFF000080)); // unset: the theme's
    CHECK(merged.dark_titlebar);

    // system_default() is an opinion: it replaces the theme's color with the OS's.
    CHECK(merge_chrome(theme, {.caption_color = FrameColor::system_default()}).caption_color ==
          FrameColor::system_default());
    CHECK_FALSE(merge_chrome(theme, {.dark_titlebar = false}).dark_titlebar);
}

TEST_CASE("Title, style and chrome set on the Window reach the host", "[chrome]")
{
    HeadlessUI ui;
    Window&    window = ui.window();

    window.set_title("Document 1");
    CHECK(ui.host().title() == "Document 1");

    window.set_style({.resizable = false, .frame = Frame::Borderless, .closable = false});
    CHECK(window.style().frame == Frame::Borderless);
    CHECK(ui.host().style() == window.style());
    CHECK_FALSE(ui.host().style().closable);
    CHECK(ui.host().style_applications == 1);

    window.set_chrome({.corner_preference = CornerPreference::DoNotRound});
    CHECK(ui.host().chrome().corner_preference == CornerPreference::DoNotRound);

    window.close();
    CHECK(ui.host().close_requests == 1);
}

// ---- non-client hit testing ----

TEST_CASE("An ordinary tree is Client everywhere", "[non-client]")
{
    HeadlessUI            ui;
    ui::Control<ui::Text> label;
    ui.window().set_content(
        ui::VerticalStack{.content = {ui::Text{.text = "Hello", .control = label}, ui::Button{.text = "OK"}}});
    ui.layout({400.0f, 300.0f});
    CHECK(ui.window().hit_region_at(label->bounds().center()) == HitRegion::Client);
    CHECK(ui.window().hit_region_at({1.0f, 1.0f}) == HitRegion::Client); // outside the content
}

TEST_CASE("A TitleBar's background is Caption; its buttons and slots are Client", "[non-client]")
{
    HeadlessUI                ui;
    ui::Control<ui::TitleBar> bar;
    ui::Control<ui::TextBox>  search;
    ui.window().set_content(ui::TitleBar{
        .title               = "App",
        .back_button_visible = true,
        .control             = bar,
        .content             = ui::TextBox{.control = search},
    });
    ui.layout({800.0f, 600.0f});

    std::vector<Element*> buttons;
    bar->collect_focusables(buttons);
    REQUIRE(buttons.size() >= 2); // back button, then the search box
    const Rect back = buttons.front()->bounds();

    CHECK(ui.window().hit_region_at(back.center()) == HitRegion::Client);
    CHECK(ui.window().hit_region_at(search->bounds().center()) == HitRegion::Client);
    // The title text and the gaps around it drag the window.
    const Point title{back.right() + 12.0f, bar->bounds().center().y};
    CHECK(ui.window().hit_region_at(title) == HitRegion::Caption);
    // Below the bar is not the bar.
    CHECK(ui.window().hit_region_at({400.0f, bar->bounds().bottom() + 10.0f}) == HitRegion::Client);
}

TEST_CASE("A drawn caption's button regions reach the host unchanged", "[non-client]")
{
    // Four 10 DIP squares: the icon, then minimize, maximize and close.
    struct DrawnCaption final : Element
    {
        HitRegion hit_region(Point p) const override
        {
            constexpr HitRegion order[] =
                {HitRegion::SystemMenu, HitRegion::MinimizeButton, HitRegion::MaximizeButton, HitRegion::CloseButton};
            return order[static_cast<int>((p.x - bounds().x) / 10.0f) % 4];
        }

    protected:
        Size measure_override(Size, const MeasureContext&) const override
        {
            return {40.0f, 10.0f};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }
    };

    HeadlessUI ui;
    ui.window().set_content(std::make_unique<DrawnCaption>());
    ui.layout({40.0f, 10.0f});
    const Rect caption = ui.window().content()->bounds();
    CHECK(ui.window().hit_region_at({caption.x + 5.0f, 5.0f}) == HitRegion::SystemMenu);
    CHECK(ui.window().hit_region_at({caption.x + 15.0f, 5.0f}) == HitRegion::MinimizeButton);
    CHECK(ui.window().hit_region_at({caption.x + 25.0f, 5.0f}) == HitRegion::MaximizeButton);
    CHECK(ui.window().hit_region_at({caption.x + 35.0f, 5.0f}) == HitRegion::CloseButton);

    SECTION("an open modal covers them like any other region")
    {
        ui.window().show_modal(std::make_unique<DialogControl>());
        ui.layout({40.0f, 10.0f});
        CHECK(ui.window().hit_region_at({caption.x + 25.0f, 5.0f}) == HitRegion::Client);
    }
}

TEST_CASE("A DragArea drags everywhere but over its focusable controls", "[non-client]")
{
    HeadlessUI              ui;
    ui::Control<ui::Text>   label;
    ui::Control<ui::Button> close;
    ui.window().set_content(ui::DragArea{
        .content = ui::VerticalStack{.content = {ui::Text{.text = "Loading...", .control = label},
                                                 ui::Button{.text = "Cancel", .control = close}}},
    });
    ui.layout({400.0f, 300.0f});

    CHECK(ui.window().hit_region_at(label->bounds().center()) == HitRegion::Caption);
    CHECK(ui.window().hit_region_at(close->bounds().center()) == HitRegion::Client);

    SECTION("an open modal is never the caption")
    {
        ui.window().show_modal(std::make_unique<DialogControl>());
        ui.layout({400.0f, 300.0f});
        CHECK(ui.window().hit_region_at(label->bounds().center()) == HitRegion::Client);
    }
}

TEST_CASE("Caption insets keep a TitleBar's right header clear of the caption buttons", "[non-client][title_bar]")
{
    HeadlessUI                ui;
    Window&                   window = ui.window();
    ui::Control<ui::TitleBar> bar;
    ui::Control<ui::Button>   share;
    window.set_content(ui::TitleBar{
        .title                = "App",
        .control              = bar,
        .right_header_content = ui::Button{.text = "Share", .control = share},
    });
    ui.layout({800.0f, 600.0f});
    const float in_content = share->bounds().right();
    CHECK_THAT(800.0f - in_content, WithinAbs(16.0f, 0.01f));

    // What the OS reports for 138 DIPs of minimize/maximize/close.
    ui.host().simulate_caption_insets({0.0f, 138.0f, 32.0f});
    ui.layout({800.0f, 600.0f});
    CHECK(bar->caption_insets().right == 138.0f);
    // Clear of the buttons plus the 48 DIP minimum drag column.
    CHECK_THAT(800.0f - share->bounds().right(), WithinAbs(138.0f + 48.0f, 0.01f));
    CHECK(window.hit_region_at({800.0f - 138.0f - 24.0f, bar->bounds().center().y}) == HitRegion::Caption);

    // Back in content: laid out as it always was.
    ui.host().simulate_caption_insets({});
    ui.layout({800.0f, 600.0f});
    CHECK_THAT(share->bounds().right(), WithinAbs(in_content, 0.01f));
}

TEST_CASE("A TitleBar dims while its window is inactive", "[non-client][title_bar]")
{
    HeadlessUI                ui;
    Window&                   window = ui.window();
    ui::Control<ui::TitleBar> bar;
    window.set_content(ui::TitleBar{.title = "App", .control = bar});
    ui.layout({800.0f, 600.0f});
    REQUIRE(bar->active());

    TestRenderer active;
    bar->render(active, window.theme());
    CHECK_FALSE(std::holds_alternative<PushOpacityLayer>(active.commands.front()));

    ui.host().simulate_active(false);
    CHECK_FALSE(bar->active());
    TestRenderer inactive;
    bar->render(inactive, window.theme());
    REQUIRE(std::holds_alternative<PushOpacityLayer>(inactive.commands.front()));
    CHECK(std::get<PushOpacityLayer>(inactive.commands.front()).opacity == 0.5f);
    CHECK(inactive.count() == active.count() + 2); // the push and its pop, around the same drawing
}

TEST_CASE("A TitleBar's set caption insets and active state win over its window's", "[non-client][title_bar]")
{
    HeadlessUI                ui;
    Window&                   window = ui.window();
    Property<bool>            focused{true};
    ui::Control<ui::TitleBar> bar;
    window.set_content(ui::TitleBar{
        .title          = "Preview",
        .caption_insets = ui::CaptionInsets{0.0f, 0.0f, 0.0f},
        .active         = focused,
        .control        = bar,
    });
    ui.host().simulate_caption_insets({0.0f, 138.0f, 32.0f});
    ui.host().simulate_active(false);
    CHECK(bar->caption_insets().right == 0.0f);
    CHECK(bar->active());

    focused = false;
    CHECK_FALSE(bar->active());

    // Unset again: back to following the window.
    bar->set_caption_insets(std::nullopt);
    bar->set_active(std::nullopt);
    CHECK(bar->caption_insets().right == 138.0f);
    CHECK_FALSE(bar->active());
    ui.host().simulate_active(true);
    CHECK(bar->active());
}

TEST_CASE("A TitleBar outside a window draws as an active in-content bar", "[non-client][title_bar]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    ui.host().simulate_caption_insets({0.0f, 138.0f, 32.0f});
    ui.host().simulate_active(false);

    ui::TitleBarControl loose{"Loose"};
    CHECK(loose.caption_insets().right == 0.0f);
    CHECK(loose.active());

    // Placed in the window it follows it, and taken out again it lets go.
    auto* stack  = window.set_content(std::make_unique<StackControl>(Orientation::Vertical));
    auto* placed = stack->add(std::make_unique<ui::TitleBarControl>("Placed"));
    CHECK(placed->caption_insets().right == 138.0f);
    CHECK_FALSE(placed->active());
    std::unique_ptr<Element> taken = stack->orphan(placed);
    REQUIRE(taken != nullptr);
    CHECK(placed->caption_insets().right == 0.0f);
    CHECK(placed->active());
}

// ---- native extension points ----

TEST_CASE("System-menu entries reach the host and come back as clicks", "[window-state][system-menu]")
{
    HeadlessUI ui;
    Window&    window  = ui.window();
    bool       on_top  = false;
    int        ignored = 0;

    std::function<void()> rebuild;
    rebuild = [&]
    {
        window.set_system_menu({
            {.text    = "Always on top",
             .checked = on_top,
             .on_click =
                 [&]
             {
                 on_top = !on_top;
                 rebuild();
             }},
            {},
            {.text = "Unavailable", .enabled = false, .on_click = [&] { ++ignored; }},
        });
    };
    rebuild();

    REQUIRE(ui.host().system_menu_texts().size() == 3);
    CHECK_FALSE(ui.host().system_menu_checks()[0]);

    // The handler re-sends the menu it lives in, to move its own check mark.
    CHECK(ui.host().click_system_menu_item(0));
    CHECK(on_top);
    CHECK(ui.host().system_menu_checks()[0]);

    CHECK_FALSE(ui.host().click_system_menu_item(1)); // a separator
    CHECK_FALSE(ui.host().click_system_menu_item(2)); // disabled
    CHECK_FALSE(ui.host().click_system_menu_item(9));
    CHECK(ignored == 0);
}

TEST_CASE("Thumbnail buttons are fixed by the first call and clicked by index", "[window-state][taskbar]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    int        plays  = 0;
    int        stops  = 0;

    window.set_thumbnail_buttons({
        {.tool_tip = "Play", .on_click = [&] { ++plays; }},
        {.tool_tip = "Stop", .enabled = false, .on_click = [&] { ++stops; }},
    });
    REQUIRE(ui.host().thumbnail_button_count() == 2);
    CHECK(ui.host().click_thumbnail_button(0));
    CHECK(plays == 1);
    CHECK_FALSE(ui.host().click_thumbnail_button(1));
    CHECK(stops == 0);

    // A later call updates the two buttons the taskbar already has, and can't add a third.
    window.set_thumbnail_buttons({
        {.tool_tip = "Pause", .on_click = [&] { --plays; }},
        {.tool_tip = "Stop", .on_click = [&] { ++stops; }},
        {.tool_tip = "Next"},
    });
    CHECK(ui.host().thumbnail_button_count() == 2);
    CHECK(ui.host().click_thumbnail_button(1));
    CHECK(stops == 1);
    CHECK_FALSE(ui.host().click_thumbnail_button(2));
}

TEST_CASE("Taskbar progress, overlay and attention reach the host", "[window-state][taskbar]")
{
    HeadlessUI ui;
    Window&    window = ui.window();

    window.taskbar_progress() = TaskbarProgress{TaskbarProgressState::Normal, 0.25};
    CHECK(ui.host().taskbar_progress() == TaskbarProgress{TaskbarProgressState::Normal, 0.25});

    window.set_taskbar_overlay(WindowIcon{.glyph = IconGlyph{0xE7E7}}, "3 unread");
    CHECK(ui.host().has_overlay());
    CHECK(ui.host().overlay_description() == "3 unread");
    window.set_taskbar_overlay(std::nullopt);
    CHECK_FALSE(ui.host().has_overlay());

    window.request_attention();
    CHECK(ui.host().flashing());
    window.cancel_attention();
    CHECK_FALSE(ui.host().flashing());

    window.set_icon(WindowIcon{.glyph = IconGlyph{0xE80F}, .background = colors::Black});
    CHECK(ui.host().has_icon());
}

TEST_CASE("WindowOptions::icon seeds the window's icon, and a resource alone is an icon", "[window-state]")
{
    CHECK(WindowIcon{}.empty());
    CHECK_FALSE(WindowIcon{.resource = "#101"}.empty());

    Window plain{{}};
    CHECK_FALSE(plain.icon().has_value());

    Window window{{.icon = WindowIcon{.resource = "APPICON"}}};
    REQUIRE(window.icon().has_value());
    CHECK(window.icon()->resource == "APPICON");
    window.set_icon(std::nullopt);
    CHECK_FALSE(window.icon().has_value());
}

TEST_CASE("on_key_preview sees keys before the focused element, and F11 can toggle full screen", "[window-state]")
{
    HeadlessUI               ui;
    Window&                  window = ui.window();
    ui::Control<ui::TextBox> field;
    window.set_content(ui::TextBox{.control = field});
    ui.layout({800.0f, 600.0f});
    window.focus_content();
    REQUIRE(window.focused() == field);

    window.on_key_preview(
        [&window](const KeyEvent& e)
        {
            if (e.key != Key::F11 || e.action != KeyAction::Down)
            {
                return ui::NotHandled;
            }
            window.toggle_fullscreen();
            return ui::Handled;
        });

    ui::testing::seams(window).handle_key({Key::F11, KeyAction::Down});
    CHECK(window.state().get() == WindowState::Fullscreen);
    ui::testing::seams(window).handle_key({Key::F11, KeyAction::Down});
    CHECK(window.state().get() == WindowState::Normal);

    // Anything the preview declines still reaches the focused field.
    ui::testing::seams(window).handle_text("a");
    ui::testing::seams(window).handle_key({Key::Home, KeyAction::Down});
    CHECK(field->text() == "a");
}

// ---- position, size and visibility ----

TEST_CASE("Move and resize reach the host; bounds and work_area read it back", "[window-state][bounds]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    ui.host().frame   = Edges{.left = 1.0f, .top = 31.0f, .right = 1.0f, .bottom = 1.0f};

    window.move({50.0f, 60.0f});
    // resize() sizes the client area; the frame comes on top, and bounds() is the outer rect.
    window.resize({500.0f, 500.0f});
    REQUIRE(window.bounds().has_value());
    CHECK(*window.bounds() == Rect{50.0f, 60.0f, 502.0f, 532.0f});

    // Centering against the work area is plain arithmetic in the same units.
    const Rect area   = *window.work_area();
    const Rect bounds = *window.bounds();
    window.move({(area.width - bounds.width) * 0.5f, (area.height - bounds.height) * 0.5f});
    CHECK_THAT(window.bounds()->center().x, WithinAbs(area.center().x, 0.01f));
}

TEST_CASE("Without a host, geometry is unknown and the requests go nowhere", "[window-state][bounds]")
{
    Window window;
    window.hide();
    CHECK_FALSE(window.bounds().has_value());
    CHECK_FALSE(window.work_area().has_value());
    CHECK_FALSE(window.visible().get()); // not shown until a host reports it
    CHECK_FALSE(window.active().get());
}

TEST_CASE("Without an OS window, move() and request_state() are kept for the one created next", "[window-state][bounds]")
{
    Window window;
    window.move({10.0f, 20.0f});
    REQUIRE(window.options().position.has_value());
    CHECK(*window.options().position == Point{10.0f, 20.0f});
    window.move({std::numeric_limits<float>::quiet_NaN(), 0.0f}); // ignored
    CHECK(*window.options().position == Point{10.0f, 20.0f});

    window.request_state(WindowState::Maximized);
    CHECK(window.options().initial_state == WindowState::Maximized);
    CHECK(window.state().get() == WindowState::Normal); // only a host reports a state
    window.restore();
    CHECK(window.options().initial_state == WindowState::Normal);
    window.toggle_fullscreen();
    CHECK(window.options().initial_state == WindowState::Fullscreen);
}

TEST_CASE("hide() and show() are reported through visible(); show() activates unless asked not to",
          "[window-state][bounds]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    REQUIRE(window.visible().get());

    window.hide();
    CHECK_FALSE(window.visible().get());
    ui.host().simulate_active(false);

    window.show_inactive();
    CHECK(window.visible().get());
    CHECK_FALSE(window.active().get());
    CHECK(ui.host().activations == 0);

    window.show();
    CHECK(window.active().get());
    CHECK(ui.host().activations == 1);

    window.activate();
    CHECK(ui.host().activations == 2);
}

TEST_CASE("Size limits are client sizes, clamp the current size and are remembered by the Window",
          "[window-state][bounds]")
{
    HeadlessUI ui;
    Window&    window = ui.window();
    ui.host().frame   = Edges{.left = 8.0f, .top = 31.0f, .right = 8.0f, .bottom = 8.0f};
    window.resize({800.0f, 500.0f});

    window.set_size_limits({400.0f, 400.0f}, {600.0f, 1000.0f});
    CHECK(window.min_size() == Size{400.0f, 400.0f});
    CHECK(window.max_size() == Size{600.0f, 1000.0f});
    CHECK(ui.host().min_size() == Size{400.0f, 400.0f});
    // 800 wide is past the maximum; 500 tall is inside the range.
    CHECK(window.bounds()->width == 616.0f);
    CHECK(window.bounds()->height == 539.0f);

    // Zero on an axis is no limit.
    window.set_size_limits({}, {0.0f, 450.0f});
    window.resize({1200.0f, 800.0f});
    CHECK(window.bounds()->width == 1216.0f);
    CHECK(window.bounds()->height == 489.0f);
}

TEST_CASE("Inactive caption colors merge like the others and default to the active ones", "[chrome]")
{
    ChromeHints theme;
    theme.caption_color = Color::from_argb(0xFFF2F6FA);
    CHECK(merge_chrome(theme, {}).inactive_caption_color.kind() == FrameColorKind::Follow);

    const ChromeHints merged = merge_chrome(theme,
                                            {.inactive_caption_color      = Color::from_argb(0xFFE5EAF0),
                                             .inactive_caption_text_color = Color::from_argb(0xFF6B7280)});
    CHECK(merged.caption_color == Color::from_argb(0xFFF2F6FA));
    CHECK(merged.inactive_caption_color == Color::from_argb(0xFFE5EAF0));
    CHECK(merged.inactive_caption_text_color == Color::from_argb(0xFF6B7280));
}

TEST_CASE("A TitleBar's content drags but for its enabled controls, and IsDragRegion overrides that",
          "[non-client][title_bar]")
{
    HeadlessUI                ui;
    ui::Control<ui::TitleBar> bar;
    ui::Control<ui::TextBox>  search;
    ui::Control<ui::Button>   status;
    ui::Control<ui::Text>     label;
    ui::ButtonControl*        back = nullptr;
    ui.window().set_content(ui::TitleBar{
        .title               = "App",
        .back_button_visible = true,
        .content_alignment   = Alignment::Stretch,
        .control             = bar,
        .content             = ui::HorizontalStack{.spacing = 8.0f,
                                                   .content =
                                                       {
                                                           ui::TextBox{.width = 200.0f, .control = search},
                                                           ui::Text{.text = "Label", .control = label},
                                                           ui::Button{.text = "Status", .control = status},
                                                       }},
    });
    ui.layout({800.0f, 600.0f});
    Window& window = ui.window();

    CHECK(window.hit_region_at(search->bounds().center()) == HitRegion::Client);
    CHECK(window.hit_region_at(status->bounds().center()) == HitRegion::Client);
    // A label in the content, and the empty space after the controls, drag.
    CHECK(window.hit_region_at(label->bounds().center()) == HitRegion::Caption);
    CHECK(window.hit_region_at({status->bounds().right() + 40.0f, status->bounds().center().y}) == HitRegion::Caption);

    SECTION("true makes a control drag, false makes anything take the press")
    {
        status->set_title_bar_drag_region(true);
        CHECK(window.hit_region_at(status->bounds().center()) == HitRegion::Caption);
        label->set_title_bar_drag_region(false);
        CHECK(window.hit_region_at(label->bounds().center()) == HitRegion::Client);
        status->set_title_bar_drag_region(std::nullopt);
        CHECK(window.hit_region_at(status->bounds().center()) == HitRegion::Client);
    }

    SECTION("a container set to true drags over its controls, unless one says false")
    {
        bar->content()->set_title_bar_drag_region(true);
        CHECK(window.hit_region_at(search->bounds().center()) == HitRegion::Caption);
        search->set_title_bar_drag_region(false);
        CHECK(window.hit_region_at(search->bounds().center()) == HitRegion::Client);
    }

    SECTION("a disabled control drags, and so does a disabled back button")
    {
        status->set_enabled(false);
        CHECK(window.hit_region_at(status->bounds().center()) == HitRegion::Caption);

        std::vector<Element*> buttons;
        bar->collect_focusables(buttons);
        back                = static_cast<ui::ButtonControl*>(buttons.front());
        const Point on_back = back->bounds().center();
        CHECK(window.hit_region_at(on_back) == HitRegion::Client);
        bar->set_back_button_enabled(false);
        CHECK(window.hit_region_at(on_back) == HitRegion::Caption);
    }
}

// ---- content arrangement and fill (tray flyouts) ----

TEST_CASE("Window content fills the client area unless it is centered", "[window-state]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    CHECK(window.stretch_content());
    Element* content = window.set_content(ui::VerticalStack{.content = {ui::Button{.text = "OK"}}});

    window.layout({400.0f, 300.0f}, measurer);
    CHECK(content->bounds() == Rect{0.0f, 0.0f, 400.0f, 300.0f});

    window.set_stretch_content(false);
    window.layout({400.0f, 300.0f}, measurer);
    const Rect centered = content->bounds();
    CHECK(centered.width < 400.0f);
    CHECK(centered.x > 0.0f);

    window.set_stretch_content(true);
    window.layout({400.0f, 300.0f}, measurer);
    CHECK(content->bounds() == Rect{0.0f, 0.0f, 400.0f, 300.0f});
}

TEST_CASE("Stretched window content still follows its own alignment and max width", "[window-state]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    Element*             centered = window.set_content(ui::VerticalStack{.horizontal_alignment = Alignment::Center,
                                                                         .vertical_alignment   = Alignment::Center,
                                                                         .content = {ui::Button{.text = "OK"}}});
    window.layout({400.0f, 300.0f}, measurer);
    CHECK(centered->bounds().width < 400.0f);
    CHECK(centered->bounds().x > 0.0f);
    CHECK(centered->bounds().y > 0.0f);

    Element* capped = window.set_content(ui::VerticalStack{.max_width = 200.0f, .content = {ui::Button{.text = "OK"}}});
    window.layout({400.0f, 300.0f}, measurer);
    CHECK(capped->bounds() == Rect{100.0f, 0.0f, 200.0f, 300.0f});
}

TEST_CASE("A window's background overrides the theme's until reset", "[window-state]")
{
    Window window;
    CHECK(window.background() == window.theme().colors().background);
    window.set_background(Color::from_argb(0xFF242424));
    CHECK(window.background() == Color::from_argb(0xFF242424));
    ThemeColors black_colors = Theme::light().colors();
    black_colors.background  = colors::Black;
    const Theme black        = Theme::fluent(black_colors);
    window.pin_theme(black);
    CHECK(window.background() == Color::from_argb(0xFF242424)); // a theme change keeps it
    window.set_background(std::nullopt);
    CHECK(window.background() == window.theme().colors().background);
}

TEST_CASE("Bound window options follow their properties for as long as the window lives", "[window-state]")
{
    Property<std::string>               title{"Draft"};
    Property<ThemeRequest>              theme{ThemeRequest{.mode = ThemeMode::Dark}};
    Property<Backdrop>                  backdrop{Backdrop::None};
    Property<std::optional<WindowIcon>> icon{};
    {
        Window window{{.title = title, .theme = theme, .backdrop = backdrop, .icon = icon}};
        CHECK(window.title() == "Draft");
        CHECK(window.theme_request().mode == ThemeMode::Dark);
        CHECK(window.theme().dark);

        title    = "Report.txt";
        theme    = ThemeRequest{.mode = ThemeMode::Light};
        backdrop = Backdrop::Mica;
        icon     = WindowIcon{.resource = "#101"};
        CHECK(window.title() == "Report.txt");
        CHECK_FALSE(window.theme().dark);
        CHECK(window.backdrop() == Backdrop::Mica);
        REQUIRE(window.icon().has_value());
        CHECK(window.icon()->resource == "#101");

        // A pinned theme holds until the bound request changes again.
        window.pin_theme(Theme::light());
        CHECK(window.theme_pinned());
        theme = ThemeRequest{.mode = ThemeMode::Dark};
        CHECK_FALSE(window.theme_pinned());
        CHECK(window.theme().dark);
    }
    title = "after the window"; // the window is gone, so nothing is told
}

TEST_CASE("The system menu takes menu items: a toggle flips, and bound fields update the entry",
          "[window-state][system-menu]")
{
    HeadlessUI            ui;
    Window&               window = ui.window();
    Property<bool>        on_top{false};
    Property<std::string> label{"Always on top"};
    Property<bool>        can_pin{true};
    int                   clicks      = 0;
    MenuItem              on_top_item = MenuItem::toggle("", on_top);
    on_top_item.text                  = label;
    window.set_system_menu({
        on_top_item,
        MenuItem::separator(),
        {.text = "Pin", .enabled = can_pin, .on_click = [&] { ++clicks; }},
    });

    REQUIRE(ui.host().system_menu_texts() == std::vector<std::string>{"Always on top", "", "Pin"});
    CHECK(ui.host().click_system_menu_item(0));
    CHECK(on_top.get());
    CHECK(ui.host().system_menu_checks()[0]);
    CHECK(window.system_menu()[0].is_checked());

    // The property drives the entry back, and the label and state follow theirs.
    on_top  = false;
    label   = "Keep on top";
    can_pin = false;
    CHECK_FALSE(ui.host().system_menu_checks()[0]);
    CHECK(ui.host().system_menu_texts()[0] == "Keep on top");
    CHECK_FALSE(ui.host().click_system_menu_item(2));
    CHECK(clicks == 0);
}

TEST_CASE("Thumbnail buttons follow bound fields and run their command", "[window-state][taskbar]")
{
    HeadlessUI     ui;
    Window&        window = ui.window();
    Property<bool> can_skip{false};
    Property<bool> can_stop{true};
    int            skips = 0;
    const Command  stop{.enabled = can_stop, .execute = [&](std::string_view) { ++skips; }};

    window.set_thumbnail_buttons({
        {.tool_tip = "Next", .enabled = can_skip, .on_click = [&] { ++skips; }},
        {.command = stop},
    });
    CHECK_FALSE(ui.host().click_thumbnail_button(0));
    can_skip = true;
    CHECK(ui.host().click_thumbnail_button(0));
    CHECK(skips == 1);

    CHECK(ui.host().click_thumbnail_button(1));
    CHECK(skips == 2);
    can_stop = false; // the command's state reaches the button
    CHECK_FALSE(ui.host().click_thumbnail_button(1));
    CHECK(skips == 2);
}

TEST_CASE("scale() is what the host reports, 1 before it does", "[window]")
{
    Window             window;
    std::vector<float> seen;
    Subscription       s = window.scale().observe([&](float scale) { seen.push_back(scale); });
    CHECK(window.scale().get() == 1.0f);
    testing::seams(window).report_scale(1.5f);
    testing::seams(window).report_scale(1.5f); // unchanged: nothing to tell
    testing::seams(window).report_scale(0.0f); // nonsense reads as 1
    CHECK(seen == std::vector<float>{1.5f, 1.0f});
}

TEST_CASE("WindowOptions::taskbar_progress seeds and follows the taskbar button's progress", "[window][taskbar]")
{
    Property<double> done{0.25};
    Window           window{
        {.taskbar_progress = ui::bind(done, [](double f) { return TaskbarProgress{TaskbarProgressState::Normal, f}; })}};
    CHECK(window.taskbar_progress().get() == TaskbarProgress{TaskbarProgressState::Normal, 0.25});
    done = 0.75;
    CHECK(window.taskbar_progress().get() == TaskbarProgress{TaskbarProgressState::Normal, 0.75});
}

// SPDX-License-Identifier: MIT
// Window states, frame styles, chrome colors and taskbar integration: the real DWM and taskbar
// calls a headless test cannot see, one control each.
#include <winui/winui.hpp>

#include <format>
#include <functional>
#include <string>
#include <vector>

#include <cstddef>

using ui::operator""_fr;

namespace
{
constexpr ui::IconGlyph AppGlyph{0xE8A7}; // OpenInNewWindow
constexpr ui::IconGlyph PreviousGlyph{0xE892};
constexpr ui::IconGlyph PlayGlyph{0xE768};
constexpr ui::IconGlyph PauseGlyph{0xE769};
constexpr ui::IconGlyph NextGlyph{0xE893};
constexpr ui::IconGlyph BadgeGlyph{0xE91F}; // a filled circle

std::string state_name(ui::WindowState state)
{
    switch (state)
    {
        case ui::WindowState::Normal:
            return "Normal";
        case ui::WindowState::Minimized:
            return "Minimized";
        case ui::WindowState::Maximized:
            return "Maximized";
        case ui::WindowState::Fullscreen:
            return "Full screen";
    }
    return {};
}

ui::Spec heading(std::string text)
{
    return ui::Text{.text = std::move(text), .role = ui::TextRole::Subtitle, .horizontal_alignment = ui::Alignment::Leading};
}

ui::Spec row(ui::SpecList children)
{
    return ui::HorizontalStack{.spacing = 8.0f, .child_alignment = ui::Alignment::Center, .content = std::move(children)};
}

} // namespace

int main()
{
    ui::WindowStyle style{.title_bar_height = 48}; // the TitleBar below is 48 DIPs tall once it has a header
    ui::Window      window{
        {.title = "libwinui — Window chrome", .size = {960, 760}, .style = style, .min_size = ui::Size{480.0f, 360.0f}}};
    ui::Window* w = &window;

    ui::Property<std::string> status{
        "Try the caption buttons, Win+arrows or F11 too — the state label follows all of them."};

    const auto restyle = [&](const std::function<void(ui::WindowStyle&)>& edit)
    {
        edit(style);
        w->set_style(style);
    };

    // "Always on top" shows up as a switch and in the system menu; both follow this.
    ui::Property<bool>    always_on_top{false};
    std::function<void()> rebuild_system_menu;
    rebuild_system_menu = [&]
    {
        w->set_system_menu({
            {.text = "Always on top", .checked = always_on_top.get(), .on_click = [&] { always_on_top.toggle(); }},
        });
    };
    rebuild_system_menu();
    const ui::Subscription on_top_sync = always_on_top.observe(
        [&](const bool& on)
        {
            restyle([on](ui::WindowStyle& s) { s.always_on_top = on; });
            rebuild_system_menu();
            status = on ? "Always on top" : "No longer on top";
        });

    ui::ChromeOverrides chrome{};

    ui::TaskbarProgress progress{ui::TaskbarProgressState::Normal, 0.0};
    w->taskbar_progress()         = progress; // what the dropdown below starts on
    bool                  playing = false;
    std::function<void()> update_thumbnail_buttons;
    update_thumbnail_buttons = [&]
    {
        w->set_thumbnail_buttons({
            {.icon     = {.glyph = PreviousGlyph},
             .tool_tip = "Previous",
             .on_click = [&] { status = "Thumbnail: previous"; }},
            {.icon     = {.glyph = playing ? PauseGlyph : PlayGlyph},
             .tool_tip = playing ? "Pause" : "Play",
             .on_click =
                 [&]
             {
                 playing = !playing;
                 status  = playing ? "Thumbnail: play" : "Thumbnail: pause";
                 update_thumbnail_buttons(); // same three buttons, new icon and tooltip
             }},
            {.icon = {.glyph = NextGlyph}, .tool_tip = "Next", .on_click = [&] { status = "Thumbnail: next"; }},
        });
    };
    update_thumbnail_buttons();

    // A flash stops once the window is in front, and clicking the button brings it there, so the
    // button only arms it and leaving the window sets it off.
    bool                   flash_when_inactive = false;
    const ui::Subscription flash_on_deactivate = w->active().observe(
        [&](const bool& active)
        {
            if (!active && flash_when_inactive)
            {
                flash_when_inactive = false;
                w->request_attention();
            }
        });

    w->set_icon(
        ui::WindowIcon{.glyph = AppGlyph, .foreground = ui::colors::White, .background = ui::Color::from_argb(0xFF0F6CBD)});

    // F11 from anywhere, whatever has focus.
    w->on_key_preview(
        [w](const ui::KeyEvent& e)
        {
            if (e.key != ui::Key::F11 || e.action != ui::KeyAction::Down)
            {
                return ui::NotHandled;
            }
            w->toggle_fullscreen();
            return ui::Handled;
        });

    window.set_content(ui::Grid{
        .columns = {1_fr},
        .rows    = {ui::AutoSize, 1_fr, ui::AutoSize},
        .content =
            {
                // ---- the title row ----
                ui::Cell{.row     = 0,
                 .column  = 0,
                 .content = ui::TitleBar{
                     .title          = "Window chrome",
                     .subtitle       = ui::bind(w->state(), state_name),
                     .icon           = AppGlyph,
                     .right_header_content   = ui::Button{
                         .text = ui::bind(w->state(),
                                      [](ui::WindowState s)
                                      { return std::string{s == ui::WindowState::Fullscreen ? "Exit full screen" : "Full screen"}; }),
                         .variant    = ui::ButtonVariant::Subtle,
                         .vertical_alignment  = ui::Alignment::Center,
                         .on_click = [w] { w->toggle_fullscreen(); },
                     },
                 }},

                // ---- the controls ----
                ui::Cell{.row     = 1,
                 .column  = 0,
                 .content = ui::Scroll{.content = ui::VerticalStack{
                     .spacing     = 12.0f,
                     .child_alignment = ui::Alignment::Leading,
                     .margin      = ui::Edges{.left = 24.0f, .top = 12.0f, .right = 24.0f, .bottom = 24.0f},
                     .content =
                         {
                             heading("State"),
                             ui::Text{.text = ui::bind(w->state(), [](ui::WindowState s) { return "The window is " + state_name(s) + "."; })},
                             row({
                                 ui::Button{.text = "Minimize", .on_click = [w] { w->request_state(ui::WindowState::Minimized); }},
                                 ui::Button{.text = "Maximize", .on_click = [w] { w->request_state(ui::WindowState::Maximized); }},
                                 ui::Button{.text = "Normal", .on_click = [w] { w->request_state(ui::WindowState::Normal); }},
                                 ui::Button{.text = "Full screen (F11)", .on_click = [w] { w->request_state(ui::WindowState::Fullscreen); }},
                                 ui::Button{.text = "Restore", .on_click = [w] { w->restore(); }},
                             }),
                             row({
                                 ui::Button{.text     = "Read placement",
                                            .on_click = [&, w]
                                            {
                                                const std::optional<ui::WindowPlacement> p = w->placement();
                                                if (p)
                                                {
                                                    status = std::format("Placement: {:.0f},{:.0f} {:.0f}x{:.0f} DIPs, {}{}",
                                                                         p->normal_bounds.x,
                                                                         p->normal_bounds.y,
                                                                         p->normal_bounds.width,
                                                                         p->normal_bounds.height,
                                                                         state_name(p->state),
                                                                         p->restore_to_maximized ? ", restores to maximized" : "");
                                                }
                                            }},
                                 ui::Button{.text = "Close", .variant = ui::ButtonVariant::Accent, .on_click = [w] { w->close(); }},
                             }),

                             heading("Frame"),
                             row({
                                 ui::Text{.text = "Frame"},
                                 ui::ComboBox{.items                = {"Standard", "Borderless", "None"},
                                              .selected_index       = 0,
                                              .on_selection_changed = [&](std::optional<std::size_t> i)
                                              {
                                                  restyle([i = i.value_or(0)](ui::WindowStyle& s) { s.frame = static_cast<ui::Frame>(i); });
                                                  status = "Frame changed — a borderless frame drags by the title row";
                                              }},
                             }),
                             ui::ToggleSwitch{.checked_text     = "Extend content into title bar",
                                              .unchecked_text   = "Extend content into title bar",
                                              .on_checked_changed = [&](bool on)
                                              { restyle([on](ui::WindowStyle& s) { s.extend_content_into_title_bar = on; }); }},
                             ui::ToggleSwitch{.checked          = style.resizable,
                                              .checked_text     = "Resizable",
                                              .unchecked_text   = "Resizable",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.resizable = on; }); }},
                             ui::ToggleSwitch{.checked          = style.minimize_button,
                                              .checked_text     = "Minimize button",
                                              .unchecked_text   = "Minimize button",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.minimize_button = on; }); }},
                             ui::ToggleSwitch{.checked          = style.maximize_button,
                                              .checked_text     = "Maximize button",
                                              .unchecked_text   = "Maximize button",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.maximize_button = on; }); }},
                             ui::ToggleSwitch{.checked          = style.closable,
                                              .checked_text     = "Closable (also blocks Alt+F4)",
                                              .unchecked_text   = "Closable (also blocks Alt+F4)",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.closable = on; }); }},
                             ui::ToggleSwitch{.checked          = always_on_top,
                                              .checked_text     = "Always on top (also in the system menu)",
                                              .unchecked_text   = "Always on top (also in the system menu)"},
                             ui::ToggleSwitch{.checked_text     = "Tool window",
                                              .unchecked_text   = "Tool window",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.tool_window = on; }); }},
                             ui::ToggleSwitch{.checked          = style.animations,
                                              .checked_text     = "Open/close/min/max animations",
                                              .unchecked_text   = "Open/close/min/max animations",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.animations = on; }); }},
                             ui::ToggleSwitch{.checked          = style.caption_title,
                                              .checked_text     = "Caption draws the title",
                                              .unchecked_text   = "Caption draws the title",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.caption_title = on; }); }},
                             ui::ToggleSwitch{.checked          = style.caption_icon,
                                              .checked_text     = "Caption draws the icon",
                                              .unchecked_text   = "Caption draws the icon",
                                              .on_checked_changed = [&](bool on) { restyle([on](ui::WindowStyle& s) { s.caption_icon = on; }); }},

                             heading("Chrome overrides"),
                             row({
                                 ui::Text{.text = "Corners"},
                                 ui::ComboBox{.items                = {"Theme", "Square", "Round", "Round small"},
                                              .selected_index       = 0,
                                              .on_selection_changed = [&](std::optional<std::size_t> i)
                                              {
                                                  constexpr ui::CornerPreference Corners[] = {ui::CornerPreference::DoNotRound,
                                                                                           ui::CornerPreference::Round,
                                                                                           ui::CornerPreference::RoundSmall};
                                                  chrome.corner_preference =
                                                      i == 0 ? std::nullopt : std::optional<ui::CornerPreference>{Corners[i.value_or(0) - 1]};
                                                  w->set_chrome(chrome);
                                              }},
                                 ui::Text{.text = "Border"},
                                 ui::ComboBox{.items                = {"Theme", "System", "None", "Red"},
                                              .selected_index       = 0,
                                              .on_selection_changed = [&](std::optional<std::size_t> i)
                                              {
                                                  const ui::FrameColor choices[] = {ui::FrameColor::follow(),
                                                                                ui::FrameColor::system_default(),
                                                                                ui::FrameColor::none(),
                                                                                ui::Color::from_argb(0xFFC42B1C)};
                                                  chrome.border_color = choices[i.value_or(0)];
                                                  w->set_chrome(chrome);
                                              }},
                                 ui::Text{.text = "Caption"},
                                 ui::ComboBox{.items                = {"Theme", "System", "Teal"},
                                              .selected_index       = 0,
                                              .on_selection_changed = [&](std::optional<std::size_t> i)
                                              {
                                                  const ui::FrameColor choices[] = {ui::FrameColor::follow(),
                                                                                ui::FrameColor::system_default(),
                                                                                ui::Color::from_argb(0xFF038387)};
                                                  chrome.caption_color = choices[i.value_or(0)];
                                                  chrome.caption_text_color =
                                                      i == 2 ? ui::FrameColor{ui::colors::White} : ui::FrameColor::follow();
                                                  w->set_chrome(chrome);
                                              }},
                             }),

                             heading("Taskbar"),
                             row({
                                 ui::Text{.text = "Progress"},
                                 ui::ComboBox{.items                = {"None", "Indeterminate", "Normal", "Paused", "Error"},
                                              .selected_index       = std::size_t{2},
                                              .on_selection_changed = [&](std::optional<std::size_t> i)
                                              {
                                                  progress.state        = static_cast<ui::TaskbarProgressState>(i.value_or(0));
                                                  w->taskbar_progress() = progress;
                                              }},
                                 ui::Slider{.value            = 0.0f,
                                            .min_width        = 200.0f,
                                            .on_value_changed = [&](double v)
                                            {
                                                progress.value        = v / 100.0f;
                                                w->taskbar_progress() = progress;
                                            }},
                             }),
                             row({
                                 ui::CheckBox{.text               = "Overlay badge",
                                              .on_checked_changed = [&, w](std::optional<bool> on)
                                              {
                                                  if (on == true)
                                                  {
                                                      w->set_taskbar_overlay(
                                                          ui::WindowIcon{.glyph = BadgeGlyph, .foreground = ui::Color::from_argb(0xFFC42B1C)},
                                                          "Something needs attention");
                                                  }
                                                  else
                                                  {
                                                      w->set_taskbar_overlay(std::nullopt);
                                                  }
                                              }},
                                 ui::Button{.text     = "Flash the taskbar button",
                                            .on_click = [&]
                                            {
                                                flash_when_inactive = true;
                                                status = "Switch to another window — the taskbar button flashes until you come back";
                                            }},
                             }),
                             ui::Text{.text = "Hover the taskbar button for the thumbnail toolbar, or press Alt+Space for the system menu."},
                         },
                 }}},

                // ---- status ----
                ui::Cell{.row     = 2,
                 .column  = 0,
                 .content = ui::Text{.text    = status,
                                     .role    = ui::TextRole::Caption,
                                     .margin  = ui::Edges{.left = 24.0f, .top = 6.0f, .right = 24.0f, .bottom = 10.0f},
                                     .horizontal_alignment = ui::Alignment::Leading}},
            },
    });

    return ui::run(window);
}

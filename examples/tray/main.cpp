// SPDX-License-Identifier: MIT
#include <winui/winui.hpp>

#include <format>
#include <string>

#include <cmath>

namespace
{
// Segoe Fluent Icons.
constexpr ui::IconGlyph VolumeGlyph{0xE767};
constexpr ui::IconGlyph QuietHoursGlyph{0xE708};

} // namespace

int main()
{
    // The model. Everything below that shows it is bound to it.
    ui::Property<float> volume{40.0f};
    ui::Property<bool>  quiet{false};

    // The flyout is an ordinary window the app owns. The preset makes it a bare, always-on-top
    // popup that is created hidden and hides instead of closing.
    ui::Window flyout{ui::tray_flyout_options({.title = "libwinui tray example"})};

    // The icon and its tooltip follow Do not disturb through bindings. The flyout is declared
    // first, so it outlives the icon that points at it.
    ui::TrayIcon tray{{
        .tool_tip = ui::bind(quiet,
                             [](bool q)
                             {
                                 return std::string{q ? "libwinui tray example — do not disturb" : "libwinui tray example"};
                             }),
        .icon     = ui::bind(quiet, [](bool q) { return ui::WindowIcon{.glyph = q ? QuietHoursGlyph : VolumeGlyph}; }),
        .flyout   = &flyout,
        // Clicking a notification opens the flyout, which is also what happens without a handler;
        // set here to show where an app would route it somewhere else.
        .on_notification_click = [&] { tray.show_flyout(); },
    }};

    // The menu is a plain value handed to the icon, so it is rebuilt when a check mark changes.
    // The app writes every item, its way out included.
    const auto sync_menu = [&]
    {
        tray.set_menu({
            {.text = "Open", .on_click = [&] { tray.show_flyout(); }},
            ui::MenuItem::toggle("Do not disturb", quiet.get(), [&](bool on) { quiet = on; }),
            ui::MenuItem::separator(),
            {.text = "Quit", .on_click = [] { ui::quit(); }},
        });
    };
    const ui::Subscription quiet_changed = quiet.observe([&] { sync_menu(); });
    sync_menu();

    flyout.set_content(ui::VerticalStack{
        .spacing = 16.0f,
        .margin  = ui::Thickness{20.0f},
        .content =
            {
                ui::Text{.text = "Quick settings", .role = ui::TextRole::Subtitle},
                ui::Slider{
                    .header = ui::bind(volume, [](float v) { return std::format("Volume: {}%", std::lround(v)); }),
                    .value  = volume,
                },
                ui::ToggleSwitch{
                    .header  = "Do not disturb",
                    .checked = quiet,
                },
                ui::Button{
                    .text    = "Send a notification",
                    .enabled = ui::bind(quiet, [](bool q) { return !q; }),
                    .on_click =
                        [&]
                    {
                        tray.notify({.title = "Hello from the tray",
                                     .text  = std::format("The volume is at {}%.", std::lround(volume.get()))});
                    },
                },
                // No Close button: like the shell's flyouts, this one closes when you click away.
                ui::Button{.text                 = "Quit",
                           .horizontal_alignment = ui::Alignment::Trailing,
                           .on_click             = [] { ui::quit(); }},
            },
    });

    // No main window: the loop runs until ui::quit(). Everything captured by [&] above lives in
    // this scope and outlives it.
    return ui::run();
}

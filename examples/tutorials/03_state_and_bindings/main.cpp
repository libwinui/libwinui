// SPDX-License-Identifier: MIT
// libwinui tutorial: state and bindings.
#include <winui/winui.hpp>

#include <format>
#include <string>

int main()
{
    // --8<-- [start:state]
    ui::Property<std::string> name{"World"};
    ui::Property<float>       volume{40.0f};
    ui::Property<bool>        muted{false};
    // --8<-- [end:state]

    // --8<-- [start:observe]
    ui::Property<int> volume_changes{0};
    ui::Subscription  counter = volume.observe([&] { ++volume_changes; });
    // --8<-- [end:observe]

    // --8<-- [start:computed]
    ui::Computed too_loud{[](float v, bool m) { return v > 80.0f && !m; }, volume, muted};
    // --8<-- [end:computed]

    ui::Window window{{.title = "State and bindings", .size = {480, 520}}};

    // --8<-- [start:tree]
    window.set_content(ui::VerticalStack{
        .spacing = 12,
        .margin  = 24,
        .content =
            {
                ui::TextBox{
                    .header = "Name",
                    .text   = name,
                },
                ui::Text{.text = ui::bind(name, [](const std::string& n) { return std::format("Hello, {}!", n); })},

                ui::Slider{
                    .header  = "Volume",
                    .value   = volume,
                    .enabled = ui::bind(muted, [](bool m) { return !m; }),
                },
                ui::ToggleSwitch{
                    .header  = "Mute",
                    .checked = muted,
                },
                ui::Text{.text = ui::bind(volume, [](float v) { return std::format("Volume {:.0f}%", v); })},
                ui::Text{.text = "That is loud.", .visible = too_loud},
                ui::Text{
                    .text   = ui::bind(volume_changes, [](int n) { return std::format("The volume changed {} times", n); }),
                    .role   = ui::TextRole::Caption,
                },

                ui::Button{
                    .text                 = "Reset volume",
                    .horizontal_alignment = ui::Alignment::Leading,
                    .on_click             = [&] {
                        volume = 40.0f;
                        muted  = false;
                    },
                },
            },
    });
    // --8<-- [end:tree]

    return ui::run(window);
}

// SPDX-License-Identifier: MIT
// libwinui tutorial: mixing imperative and declarative.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
#include <memory>
#include <string>
#include <vector>
// --8<-- [end:includes]


namespace
{
// --8<-- [start:hand_built]
std::unique_ptr<ui::StackControl> make_legend(const std::vector<std::string>& tags)
{
    auto legend = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
    legend->set_spacing(12.0f);
    for (const std::string& tag : tags)
    {
        ui::TextControl* label = legend->add(std::make_unique<ui::TextControl>(tag));
        label->set_role(ui::TextRole::Caption);
    }
    return legend;
}
// --8<-- [end:hand_built]

// --8<-- [start:component]
ui::Spec legend_card(const std::vector<std::string>& tags)
{
    return ui::Border{
        .border_brush     = ui::Color::from_argb(0x33000000),
        .border_thickness = 1,
        .corner_radius    = 4,
        .padding          = 8,
        .content          = make_legend(tags),
    };
}
// --8<-- [end:component]

} // namespace

int main()
{
    // --8<-- [start:state]
    const std::vector<std::string> tags    = {"inbox", "work", "family", "later"};
    int                            entries = 0;
    ui::Control<ui::Text>          counter;
    ui::Control<ui::Button>        clear;
    ui::Control<ui::VerticalStack> history;
    // --8<-- [end:state]

    // --8<-- [start:add_line]
    auto add_line = [&](const std::string& text)
    {
        ui::TextControl* line = history->add(std::make_unique<ui::TextControl>(text));
        line->set_role(ui::TextRole::Caption);
    };
    // --8<-- [end:add_line]

    // --8<-- [start:loop]
    ui::SpecList tag_buttons;
    for (const std::string& tag : tags)
    {
        tag_buttons.push_back(ui::Button{
            .text = tag,
            .on_click =
                [&, tag]
            {
                add_line(std::format("{}. Tagged '{}'", ++entries, tag));
                counter->set_text(std::format("{} entries", entries));
                clear->set_enabled(true);
            },
        });
    }
    // --8<-- [end:loop]

    ui::Window window{{.title = "Mixing imperative and declarative", .size = {560, 520}}};

    // --8<-- [start:tree]
    window.set_content(ui::VerticalStack{
        .spacing = 12,
        .margin  = 24,
        .content =
            {
                ui::Text{.text = "Tag the current message", .role = ui::TextRole::Subtitle},
                legend_card(tags),
                ui::HorizontalStack{.spacing = 8, .content = std::move(tag_buttons)},
                ui::HorizontalStack{
                    .spacing = 12,
                    .content =
                        {
                            ui::Text{.text = "0 entries", .vertical_alignment = ui::Alignment::Center, .control = counter},
                            ui::Button{
                                .text    = "Clear",
                                .enabled = false,
                                .on_click =
                                    [&]
                                {
                                    history->clear();
                                    entries = 0;
                                    counter->set_text("0 entries");
                                    clear->set_enabled(false);
                                },
                                .control = clear,
                            },
                        },
                },
                ui::Scroll{.height = 240, .content = ui::VerticalStack{.spacing = 4, .control = history}},
            },
    });
    // --8<-- [end:tree]

    // --8<-- [start:after]
    history->add(ui::Text{.text = "Entries appear here."});
    // --8<-- [end:after]

    return ui::run(window);
}

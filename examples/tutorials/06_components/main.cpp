// SPDX-License-Identifier: MIT
// libwinui tutorial: components.
#include <winui/winui.hpp>

#include <format>
#include <string>


namespace
{
// --8<-- [start:section]
ui::Spec section(std::string title, ui::SpecList body)
{
    return ui::VerticalStack{
        .spacing = 8,
        .content =
            {
                ui::Text{.text = std::move(title), .role = ui::TextRole::Subtitle},
                ui::VerticalStack{.spacing = 8, .content = std::move(body)},
            },
    };
}
// --8<-- [end:section]

// --8<-- [start:level_row]
ui::Spec level_row(std::string name, ui::Property<float>& level)
{
    return ui::HorizontalStack{
        .spacing         = 12,
        .child_alignment = ui::Alignment::Center,
        .content =
            {
                ui::Text{.text = std::move(name), .width = 80},
                ui::Slider{
                    .value = level,
                    .width = 200,
                },
                ui::Text{.text = ui::bind(level, [](float v) { return std::format("{:.0f}", v); }), .width = 40},
            },
    };
}
// --8<-- [end:level_row]

// --8<-- [start:filter_box]
ui::Spec filter_box(ui::Property<std::string>& query, ui::Control<ui::TextBox>& control)
{
    return ui::TextBox{
        .header           = "Filter",
        .placeholder_text = "Type to filter",
        .text             = query,
        .control          = control,
    };
}
// --8<-- [end:filter_box]

// --8<-- [start:channel]
struct Channel
{
    std::string         name;
    ui::Property<float> level;
};
// --8<-- [end:channel]

} // namespace

int main()
{
    // --8<-- [start:state]
    Channel channels[] = {
        {"Music", ui::Property<float>{80.0f}},
        {"Voice", ui::Property<float>{60.0f}},
        {"Effects", ui::Property<float>{40.0f}},
    };
    ui::Property<std::string> query{};
    // --8<-- [end:state]

    // --8<-- [start:list]
    ui::SpecList rows;
    for (Channel& channel : channels)
    {
        rows.push_back(level_row(channel.name, channel.level));
    }
    // --8<-- [end:list]

    // --8<-- [start:tree]
    ui::Control<ui::TextBox> filter;

    ui::Window window{{.title = "Components", .size = {480, 420}}};

    window.set_content(ui::VerticalStack{
        .spacing = 24,
        .margin  = 24,
        .content =
            {
                section("Mixer", std::move(rows)),
                section("Search",
                        {
                            filter_box(query, filter),
                            ui::Text{
                                .text = ui::bind(query,
                                                 [](const std::string& q)
                                                 {
                                                     return q.empty() ? std::string{"No filter"}
                                                                      : std::format("Filtering for \"{}\"", q);
                                                 }),
                                .role = ui::TextRole::Caption,
                            },
                        }),
            },
    });
    window.focus_element(filter);
    // --8<-- [end:tree]

    return ui::run(window);
}

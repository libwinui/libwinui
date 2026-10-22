// SPDX-License-Identifier: MIT
// libwinui tutorial: layout.
#include <winui/winui.hpp>

#include <utility>


int main()
{
    // --8<-- [start:literals]
    using ui::operator""_dip;
    using ui::operator""_fr;
    // --8<-- [end:literals]

    // --8<-- [start:stacks]
    ui::Spec stacks = ui::VerticalStack{
        .spacing = 8,
        .content =
            {
                ui::Text{.text = "Stacks", .role = ui::TextRole::Subtitle},
                ui::HorizontalStack{
                    .spacing = 8,
                    .content =
                        {
                            ui::Button{.text = "One"},
                            ui::Button{.text = "Two"},
                            ui::Button{.text = "Three"},
                        },
                },
            },
    };
    // --8<-- [end:stacks]

    // --8<-- [start:alignment]
    ui::Spec alignment = ui::VerticalStack{
        .spacing = 8,
        .content =
            {
                ui::Text{.text = "Alignment and margin", .role = ui::TextRole::Subtitle},
                ui::Button{.text = "Stretch (the default)"},
                ui::Button{.text = "Leading", .horizontal_alignment = ui::Alignment::Leading},
                ui::Button{.text = "Center", .horizontal_alignment = ui::Alignment::Center},
                ui::Button{.text = "Trailing", .horizontal_alignment = ui::Alignment::Trailing},
                ui::Button{.text                 = "Leading, 32 DIPs in from the left",
                           .margin               = ui::Edges{.left = 32},
                           .horizontal_alignment = ui::Alignment::Leading},
            },
    };
    // --8<-- [end:alignment]

    // --8<-- [start:sizes]
    ui::Spec sizes = ui::VerticalStack{
        .spacing = 8,
        .content =
            {
                ui::Text{.text = "Sizes", .role = ui::TextRole::Subtitle},
                ui::HorizontalStack{
                    .spacing = 8,
                    .content =
                        {
                            ui::Button{.text = "OK", .min_width = 120},
                            ui::Button{.text = "Cancel", .min_width = 120},
                            ui::Button{.text = "A label longer than the minimum", .min_width = 120},
                        },
                },
                ui::Button{.text = "Fixed 200 x 48", .width = 200, .height = 48, .horizontal_alignment = ui::Alignment::Leading},
                ui::Button{.text = "Stretched, but at most 320 wide", .max_width = 320},
            },
    };
    // --8<-- [end:sizes]

    // --8<-- [start:grid]
    ui::Spec grid = ui::VerticalStack{
        .spacing = 8,
        .content =
            {
                ui::Text{.text = "Grid", .role = ui::TextRole::Subtitle},
                ui::Grid{
                    .columns        = {120_dip, 1_fr, ui::AutoSize},
                    .rows           = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                    .row_spacing    = 8,
                    .column_spacing = 12,
                    .content =
                        {
                            ui::Cell{.row     = 0,
                                     .column  = 0,
                                     .content = ui::Text{.text = "Name", .vertical_alignment = ui::Alignment::Center}},
                            ui::Cell{.row = 0, .column = 1, .content = ui::TextBox{.placeholder_text = "Ada Lovelace"}},
                            ui::Cell{.row     = 1,
                                     .column  = 0,
                                     .content = ui::Text{.text = "Email", .vertical_alignment = ui::Alignment::Center}},
                            ui::Cell{.row = 1, .column = 1, .content = ui::TextBox{.placeholder_text = "ada@example.com"}},
                            ui::Cell{.row = 0, .column = 2, .row_span = 2, .content = ui::Button{.text = "Look up"}},
                            ui::Cell{.row         = 2,
                                     .column      = 1,
                                     .column_span = 2,
                                     .content = ui::Text{.text = "This cell spans two columns.", .role = ui::TextRole::Caption}},
                        },
                },
            },
    };
    // --8<-- [end:grid]

    // --8<-- [start:canvas]
    // A dialog laid out by coordinates, the way a resource editor would have saved it.
    ui::Spec canvas = ui::VerticalStack{
        .spacing = 8,
        .content =
            {
                ui::Text{.text = "Canvas", .role = ui::TextRole::Subtitle},
                ui::Canvas{
                    .width  = 360,
                    .height = 112,
                    .content =
                        {
                            ui::CanvasItem{.left = 0, .top = 6, .content = ui::Text{.text = "Find what:"}},
                            ui::CanvasItem{.left = 80, .top = 0, .content = ui::TextBox{.width = 180}},
                            ui::CanvasItem{.left = 272, .top = 0, .content = ui::Button{.text = "Find Next", .width = 88}},
                            ui::CanvasItem{.left = 272, .top = 40, .content = ui::Button{.text = "Cancel", .width = 88}},
                            ui::CanvasItem{.left = 0, .top = 46, .content = ui::CheckBox{.text = "Match case"}},
                            ui::CanvasItem{.left = 0, .top = 80, .content = ui::CheckBox{.text = "Wrap around"}},
                        },
                },
            },
    };
    // --8<-- [end:canvas]

    ui::Window window{{.title = "Layout", .size = {560, 480}}};

    // --8<-- [start:scroll]
    window.set_content(ui::Scroll{
        .content =
            ui::VerticalStack{
                .spacing = 24,
                .margin  = 24,
                .content =
                    {
                        std::move(stacks),
                        std::move(alignment),
                        std::move(sizes),
                        std::move(grid),
                        std::move(canvas),
                    },
            },
    });
    // --8<-- [end:scroll]

    return ui::run(window);
}

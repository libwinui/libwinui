// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cmath>
#include <cstddef>


using ui::operator""_dip;
using ui::operator""_fr;

namespace gallery
{
namespace
{
// The named colors the samples fill their rectangles with.
constexpr ui::Color Red       = ui::Color::from_argb(0xFFFF0000);
constexpr ui::Color Blue      = ui::Color::from_argb(0xFF0000FF);
constexpr ui::Color Green     = ui::Color::from_argb(0xFF008000);
constexpr ui::Color Yellow    = ui::Color::from_argb(0xFFFFFF00);
constexpr ui::Color Gray      = ui::Color::from_argb(0xFF808080);
constexpr ui::Color DarkGray  = ui::Color::from_argb(0xFFA9A9A9);
constexpr ui::Color Gold      = ui::Color::from_argb(0xFFFFD700);
constexpr ui::Color DarkGreen = ui::Color::from_argb(0xFF006400);
constexpr ui::Color DarkBlue  = ui::Color::from_argb(0xFF00008B);

ui::Spec options_panel(float width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// A Rectangle of the samples: a fixed size, or 0 to fill the slot on that axis.
ui::Spec rect(ui::Color color, float width = 40.0f, float height = 40.0f)
{
    return Swatch{.fill = color, .width = width, .height = height};
}

// A vertical Slider with its header 10 above it, reversed so the top is 0.
ui::Spec vertical_slider(const char* header, float max, float height, ui::Property<float>& value, float tick = 0.0f)
{
    return ui::VerticalStack{
        .spacing            = 10.0f,
        .vertical_alignment = ui::Alignment::Leading,
        .content =
            {
                ui::Text{.text = header, .horizontal_alignment = ui::Alignment::Leading},
                ui::Slider{.value              = value,
                           .min                = 0.0f,
                           .max                = max,
                           .tick_frequency     = tick,
                           .snaps_to           = tick > 0.0f ? ui::SliderSnapsTo::Ticks : ui::SliderSnapsTo::StepValues,
                           .orientation        = ui::Orientation::Vertical,
                           .direction_reversed = true,
                           .height             = height,
                           .horizontal_alignment = ui::Alignment::Leading},
            },
    };
}

ui::Spec horizontal_slider(const char* header, float min, float max, ui::Property<float>& value, float tick = 0.0f, float step = 1.0f)
{
    return ui::Slider{.header         = header,
                      .value          = value,
                      .min            = min,
                      .max            = max,
                      .tick_frequency = tick,
                      .step_frequency = step,
                      .snaps_to       = tick > 0.0f ? ui::SliderSnapsTo::Ticks : ui::SliderSnapsTo::StepValues};
}

std::size_t to_index(float v)
{
    return static_cast<std::size_t>(std::max(0.0f, v + 0.5f));
}

// ---- Border ----

struct BorderState
{
    ui::Property<float>     thickness{2.0f};
    ui::Property<ui::Color> background{ui::colors::White};
    ui::Property<ui::Color> brush{Gold};
    ui::Property<bool>      clip{true};
};

// Four colored quarters, so the corners a clip cuts away are easy to see.
ui::Spec quarters()
{
    const auto quarter = [](ui::Color color, int row, int column)
    { return ui::Cell{.row = row, .column = column, .content = ui::Border{.background = color}}; };
    return ui::Grid{
        .columns = {1_fr, 1_fr},
        .rows    = {1_fr, 1_fr},
        .content = {quarter(Green, 0, 0), quarter(Yellow, 0, 1), quarter(Blue, 1, 0), quarter(DarkBlue, 1, 1)},
    };
}

ui::Spec border_page(Context&)
{
    return with_state<BorderState>(
        [](BorderState& s)
        {
            constexpr std::array<ui::Color, 4> Backgrounds{Green, Yellow, Blue, ui::colors::White};
            constexpr std::array<ui::Color, 4> Brushes{DarkGreen, Gold, DarkBlue, ui::colors::White};
            ui::TextStyle                      text = ui::Theme::light().text_style;
            text.font_size                          = 18.0f;
            return examples(
                {
                    example(
                        {
                            .title = "A Border around a Text.",
                            .code  = R"cpp(
                                ui::Property<float>     thickness{2.0f};
                                ui::Property<ui::Color> background{ui::colors::White};
                                ui::Property<ui::Color> brush{ui::Color::from_argb(0xFFFFD700)};

                                ui::Border{
                                    .background       = ui::bind(background, [](ui::Color c) { return std::optional<ui::Color>{c}; }),
                                    .border_brush     = ui::bind(brush, [](ui::Color c) { return std::optional<ui::Color>{c}; }),
                                    .border_thickness = ui::bind(thickness, [](float t) { return ui::Thickness{t}; }),
                                    .vertical_alignment = ui::Alignment::Leading,
                                    .content = ui::Text{
                                        .text   = "Text inside a border",
                                        .margin = ui::Symmetric{.horizontal = 8.0f, .vertical = 5.0f},
                                    },
                                }

                                // The options write the properties, the border follows.
                                thickness = 4.0f;
                                brush     = ui::Color::from_argb(0xFF006400);
                            )cpp",
                            .options = options_panel(260.0f,
                                                     {
                                                         horizontal_slider("BorderThickness", 0.0f, 10.0f, s.thickness),
                                                         ui::Grid{.columns        = {1_fr, 1_fr},
                                                                  .rows           = {ui::AutoSize},
                                                                  .column_spacing = 8.0f,
                                                                  .content =
                                                                      {
                                                                          ui::Cell{.row    = 0,
                                                                                   .column = 0,
                                                                                   .content = ui::RadioButtons{.header = "Background",
                                                                                                               .items = {"Green",
                                                                                                                         "Yellow",
                                                                                                                         "Blue",
                                                                                                                         "White"},
                                                                                                               .selected_index = 3,
                                                                                                               .on_selection_changed =
                                                                                                                   [&s, Backgrounds](std::optional<std::size_t> i)
                                                                                                               {
                                                                                                                   if (i.value_or(
                                                                                                                           0) <
                                                                                                                       4)
                                                                                                                   {
                                                                                                                       s.background = Backgrounds[static_cast<
                                                                                                                           std::size_t>(
                                                                                                                           i.value_or(0))];
                                                                                                                   }
                                                                                                               }}},
                                                                          ui::Cell{.row    = 0,
                                                                                   .column = 1,
                                                                                   .content = ui::RadioButtons{.header = "Border brush",
                                                                                                               .items = {"Green",
                                                                                                                         "Yellow",
                                                                                                                         "Blue",
                                                                                                                         "White"},
                                                                                                               .selected_index = 1,
                                                                                                               .on_selection_changed =
                                                                                                                   [&s, Brushes](std::optional<std::size_t> i)
                                                                                                               {
                                                                                                                   if (i.value_or(
                                                                                                                           0) <
                                                                                                                       4)
                                                                                                                   {
                                                                                                                       s.brush = Brushes[static_cast<
                                                                                                                           std::size_t>(
                                                                                                                           i.value_or(0))];
                                                                                                                   }
                                                                                                               }}},
                                                                      }},
                                                     }),
                            .controls =
                                {
                                    ui::Border{.background         = ui::bind(s.background,
                                                                              [](ui::Color c)
                                                                              { return std::optional<ui::Color>{c}; }),
                                               .border_brush       = ui::bind(s.brush,
                                                                              [](ui::Color c)
                                                                              { return std::optional<ui::Color>{c}; }),
                                               .border_thickness   = ui::bind(s.thickness,
                                                                              [](float t) { return ui::Thickness{t}; }),
                                               .vertical_alignment = ui::Alignment::Leading,
                                               .content = ui::Text{.text       = "Text inside a border",
                                                                   .text_style = text,
                                                                   .foreground = ui::colors::Black,
                                                                   .margin = ui::Symmetric{.horizontal = 8.0f, .vertical = 5.0f}}},
                                },
                        }),
                    example({
                        .title = "Content clipped to the rounded corners.",
                        .code  = R"cpp(
                                ui::Property<bool> clip{true};
                                ui::ImageSource    photo = ui::Bitmap::read("photo.jpg");

                                ui::Border{
                                    .corner_radius        = 16.0f,
                                    .clip_content         = ui::bind(clip),
                                    .width                = 240.0f,
                                    .height               = 160.0f,
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .content              = ui::Image{.source = photo, .stretch = ui::Stretch::UniformToFill},
                                }
                            )cpp",
                        .options = options_panel(260.0f,
                                                 {ui::ToggleSwitch{.header = "Clip content", .checked = ui::bind(s.clip)}}),
                        .controls = {ui::Border{.corner_radius        = 16.0f,
                                                .clip_content         = ui::bind(s.clip),
                                                .width                = 240.0f,
                                                .height               = 160.0f,
                                                .horizontal_alignment = ui::Alignment::Leading,
                                                .content              = quarters()}},
                    }),
                });
        });
}

// ---- Canvas ----

struct CanvasState
{
    ui::Property<float>     left{0.0f};
    ui::Property<float>     top{0.0f};
    ui::Property<float>     z{0.0f};
    ui::Control<ui::Slider> z_slider;
};

// The Z slider's accessible name carries its value.
void name_z_slider(ui::SliderControl* slider, float value)
{
    if (slider != nullptr)
    {
        slider->set_accessibility(
            {.name = "Canvas.ZIndex value " + std::to_string(static_cast<int>(value)) + " of range 0 to 4"});
    }
}

ui::Spec canvas_page(Context&)
{
    return with_state<CanvasState>(
        [](CanvasState& s)
        {
            auto page = examples({
                example({
                    .title = "A Canvas control.",
                    .code  = R"cpp(
                        const ui::Color red    = ui::Color::from_argb(0xFFFF0000);
                        const ui::Color blue   = ui::Color::from_argb(0xFF0000FF);
                        const ui::Color green  = ui::Color::from_argb(0xFF008000);
                        const ui::Color yellow = ui::Color::from_argb(0xFFFFFF00);

                        // Each is bound to a slider.
                        ui::Property<float> left{0.0f};
                        ui::Property<float> top{0.0f};
                        ui::Property<float> z{0.0f};

                        ui::Canvas{
                            .background = ui::Color::from_argb(0xFF808080),
                            .width      = 140,
                            .height     = 140,
                            .content    = {
                                ui::CanvasItem{.left    = left,
                                 .top     = top,
                                 .z_index = ui::bind(z, [](float v) { return static_cast<int>(std::lround(v)); }),
                                 .content = ui::Rectangle{.fill = red, .width = 40, .height = 40}},
                                ui::CanvasItem{.left = 20, .top = 20, .z_index = 1, .content = ui::Rectangle{.fill = blue, .width = 40, .height = 40}},
                                ui::CanvasItem{.left = 40, .top = 40, .z_index = 2, .content = ui::Rectangle{.fill = green, .width = 40, .height = 40}},
                                ui::CanvasItem{.left = 60, .top = 60, .z_index = 3, .content = ui::Rectangle{.fill = yellow, .width = 40, .height = 40}},
                            },
                        }
                    )cpp",
                    .options =
                        ui::HorizontalStack{
                            .content =
                                {
                                    vertical_slider("Canvas.Top", 100.0f, 110.0f, s.top),
                                    ui::VerticalStack{
                                        .margin = ui::Edges{.left = 16.0f},
                                        .content =
                                            {
                                                ui::Slider{.header           = "Canvas.Left",
                                                           .value            = s.left,
                                                           .max              = 100.0f,
                                                           .width            = 100.0f,
                                                           .horizontal_alignment          = ui::Alignment::Leading},
                                                ui::Slider{.header = "Canvas.ZIndex",
                                                           .value  = s.z,
                                                           .max    = 4.0f,
                                                           .width  = 100.0f,
                                                           .horizontal_alignment = ui::Alignment::Leading,
                                                           .on_value_changed =
                                                               [&s](double v)
                                                           {
                                                               s.z = static_cast<float>(static_cast<float>(v));
                                                               name_z_slider(s.z_slider, static_cast<float>(v));
                                                           },
                                                           .control = s.z_slider},
                                            }},
                                }},
                    .controls =
                        {
                            ui::Canvas{.background = Gray,
                                       .width      = 140.0f,
                                       .height     = 140.0f,
                                       .vertical_alignment    = ui::Alignment::Leading,
                                       .content =
                                           {
                                               ui::CanvasItem{.left    = s.left,
                                                .top     = s.top,
                                                .z_index = ui::bind(s.z, [](float v) { return static_cast<int>(std::lround(v)); }),
                                                .content = rect(Red)},
                                               ui::CanvasItem{.left = 20.0f, .top = 20.0f, .z_index = 1, .content = rect(Blue)},
                                               ui::CanvasItem{.left = 40.0f, .top = 40.0f, .z_index = 2, .content = rect(Green)},
                                               ui::CanvasItem{.left = 60.0f, .top = 60.0f, .z_index = 3, .content = rect(Yellow)},
                                           }},
                        },
                }),
            });
            name_z_slider(s.z_slider, 0.0f);
            return page;
        });
}

// ---- Expander ----

struct ExpanderState
{
    ui::Property<ui::ExpandDirection> direction{ui::ExpandDirection::Down};
    ui::Control<ui::Expander>         expander;
};

ui::Spec expander_page(Context& ctx)
{
    auto& status = ctx.status;
    return with_state<ExpanderState>(
        [&status](ExpanderState& s)
        {
            return examples({
                example({
                    .title   = "An Expander with text in the header and content areas",
                    .code    = R"cpp(
                        ui::Property<ui::ExpandDirection> direction{ui::ExpandDirection::Down};
                        ui::Control<ui::Expander>         expander;

                        ui::Expander{
                            .header              = "This text is in the header",
                            .expand_direction    = direction,
                            .vertical_alignment  = ui::Alignment::Leading,
                            .on_expanded_changed = [](bool open) { /* expanded or collapsed */ },
                            .control             = expander,
                            .content             = ui::Text{
                                .text                 = "This is in the content",
                                .horizontal_alignment = ui::Alignment::Leading,
                            },
                        }

                        // An expander that opens upwards sits at the bottom of its slot.
                        direction = ui::ExpandDirection::Up;
                        expander->set_vertical_alignment(ui::Alignment::Trailing);
                    )cpp",
                    .options = options_panel(160.0f,
                                             {
                                                 ui::ComboBox{.header         = "Expand direction",
                                                              .items          = {"Down", "Up"},
                                                              .selected_index = 0,
                                                              .on_selection_changed =
                                                                  [&s](std::optional<std::size_t> i)
                                                              {
                                                                  // The Expander sample pairs Up with a bottom alignment.
                                                                  s.direction = i == 1 ? ui::ExpandDirection::Up
                                                                                       : ui::ExpandDirection::Down;
                                                                  if (s.expander != nullptr)
                                                                  {
                                                                      s.expander->set_vertical_alignment(
                                                                          i == 1 ? ui::Alignment::Trailing
                                                                                 : ui::Alignment::Leading);
                                                                  }
                                                              }},
                                             }),
                    .controls =
                        {
                            ui::Expander{.header              = "This text is in the header",
                                         .expand_direction    = s.direction,
                                         .vertical_alignment  = ui::Alignment::Leading,
                                         .on_expanded_changed = [&status](bool open)
                                         { status = open ? "Expander: expanded" : "Expander: collapsed"; },
                                         .control = s.expander,
                                         .content = ui::Text{.text                 = "This is in the content",
                                                             .horizontal_alignment = ui::Alignment::Leading}},
                        },
                }),
                example({
                    .title = "Modifying Expanders content alignment",
                    .code  = R"cpp(
                        ui::Expander{
                            .content_padding = ui::Thickness{0.0f},
                            .width           = 500,
                            .header_content  = ui::Text{
                                .text                 = "This text is centered",
                                .horizontal_alignment = ui::Alignment::Center,
                            },
                            .content         = ui::Text{
                                .text                 = "And this text is left aligned",
                                .margin               = ui::Thickness{4.0f},
                                .horizontal_alignment = ui::Alignment::Leading,
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::Expander{.content_padding = ui::Thickness{0.0f},
                                         .width           = 500.0f,
                                         .header_content  = ui::Text{.text                 = "This text is centered",
                                                                     .horizontal_alignment = ui::Alignment::Center},
                                         .content         = ui::Text{.text                 = "And this text is left aligned",
                                                                     .margin               = ui::Thickness{4.0f},
                                                                     .horizontal_alignment = ui::Alignment::Leading}},
                        },
                }),
            });
        });
}

// ---- GroupBox ----

struct GroupBoxState
{
    ui::Property<std::string> header{"Appearance"};
};

ui::Spec group_box_page(Context&)
{
    return with_state<GroupBoxState>(
        [](GroupBoxState& s)
        {
            ui::Spec settings = ui::VerticalStack{
                .spacing = 8.0f,
                .content =
                    {
                        ui::CheckBox{.text = "Show the status bar", .checked = true},
                        ui::CheckBox{.text = "Show line numbers"},
                        ui::CheckBox{.text = "Wrap long lines"},
                    },
            };
            return examples({
                example({
                    .title    = "A GroupBox around related settings",
                    .code     = R"cpp(
                        ui::Property<std::string> header{"Appearance"};

                        ui::GroupBox{
                            .header               = header,
                            .horizontal_alignment = ui::Alignment::Leading,
                            .content              = ui::VerticalStack{
                                .spacing = 8,
                                .content = {
                                    ui::CheckBox{.text = "Show the status bar", .checked = true},
                                    ui::CheckBox{.text = "Show line numbers"},
                                    ui::CheckBox{.text = "Wrap long lines"},
                                },
                            },
                        }
                    )cpp",
                    .options  = options_panel(220.0f,
                                              {
                                                  ui::TextBox{.header = "Header", .text = s.header},
                                              }),
                    .controls = {ui::GroupBox{.header               = s.header,
                                              .horizontal_alignment = ui::Alignment::Leading,
                                              .content              = std::move(settings)}},
                }),
                example({
                    .title    = "Radio buttons in a GroupBox",
                    .code     = R"cpp(
                        ui::GroupBox{
                            .header               = "Line endings",
                            .horizontal_alignment = ui::Alignment::Leading,
                            .content              = ui::RadioButtons{
                                .items          = {"Windows (CRLF)", "Unix (LF)", "Classic Mac (CR)"},
                                .selected_index = 0,
                            },
                        }
                    )cpp",
                    .controls = {ui::GroupBox{
                        .header               = "Line endings",
                        .horizontal_alignment = ui::Alignment::Leading,
                        .content = ui::RadioButtons{.items = {"Windows (CRLF)", "Unix (LF)", "Classic Mac (CR)"},
                                                    .selected_index = 0},
                    }},
                }),
            });
        });
}

// ---- Grid ----

struct GridState
{
    ui::Property<float> column_spacing{8.0f};
    ui::Property<float> row_spacing{8.0f};
    ui::Property<float> column{0.0f};
    ui::Property<float> row{0.0f};
};

ui::Spec grid_page(Context&)
{
    return with_state<GridState>(
        [](GridState& s)
        {
            auto spaced = [](ui::Spec view)
            { return ui::VerticalStack{.margin = ui::Edges{.left = 16.0f}, .content = {std::move(view)}}; };
            return examples(
                {
                    example(
                        {
                            .title   = "A 3x3 Grid control.",
                            .code    = R"cpp(
                        const ui::Color red    = ui::Color::from_argb(0xFFFF0000);
                        const ui::Color blue   = ui::Color::from_argb(0xFF0000FF);
                        const ui::Color green  = ui::Color::from_argb(0xFF008000);
                        const ui::Color yellow = ui::Color::from_argb(0xFFFFFF00);

                        ui::Property<float>       row_spacing{8.0f};
                        ui::Property<float>       column_spacing{8.0f};
                        // Where the red block sits.
                        ui::Property<std::size_t> row{0};
                        ui::Property<std::size_t> column{0};

                        ui::Grid{
                            .columns        = {50_dip, 50_dip, 50_dip},
                            .rows           = {50_dip, 50_dip, 50_dip},
                            .row_spacing    = row_spacing,
                            .column_spacing = column_spacing,
                            .background     = ui::Color::from_argb(0xFF808080),
                            .width          = 240,
                            .height         = 160,
                            .content          = {
                                ui::Cell{.row     = row,
                                 .column  = column,
                                 .content = ui::Rectangle{.fill = red, .width = 50, .height = 50}},
                                ui::Cell{.row = 1, .column = 0, .content = ui::Rectangle{.fill = blue, .width = 50, .height = 50}},
                                ui::Cell{.row = 0, .column = 1, .content = ui::Rectangle{.fill = green, .width = 50, .height = 50}},
                                ui::Cell{.row = 1, .column = 1, .content = ui::Rectangle{.fill = yellow, .width = 50, .height = 50}},
                            },
                        }
                    )cpp",
                            .options = ui::
                                Grid{.columns        = {ui::AutoSize, ui::AutoSize},
                                     .rows           = {ui::AutoSize, ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                     .row_spacing    = 12.0f,
                                     .column_spacing = 12.0f,
                                     .content =
                                         {
                                             ui::Cell{.row    = 0,
                                                      .column = 0,
                                                      .content = ui::Text{.text = "Grid",
                                                                          .horizontal_alignment = ui::Alignment::Leading}},
                                             ui::Cell{.row     = 1,
                                                      .column  = 0,
                                                      .content = spaced(
                                                          horizontal_slider("ColumnSpacing", 0.0f, 16.0f, s.column_spacing, 1.0f))},
                                             ui::Cell{.row    = 1,
                                                      .column = 1,
                                                      .content = vertical_slider("RowSpacing", 16.0f, 100.0f, s.row_spacing, 1.0f)},
                                             ui::Cell{.row    = 2,
                                                      .column = 0,
                                                      .content = ui::Text{.text = "Red block",
                                                                          .horizontal_alignment = ui::Alignment::Leading}},
                                             ui::Cell{.row     = 3,
                                                      .column  = 0,
                                                      .content = spaced(
                                                          horizontal_slider("Grid.Column", 0.0f, 2.0f, s.column, 1.0f))},
                                             ui::Cell{.row    = 3,
                                                      .column = 1,
                                                      .content = vertical_slider("Grid.Row", 2.0f, 100.0f, s.row, 1.0f)},
                                         }},
                            .controls =
                                {
                                    ui::Grid{.columns        = {50_dip, 50_dip, 50_dip},
                                             .rows           = {50_dip, 50_dip, 50_dip},
                                             .row_spacing    = s.row_spacing,
                                             .column_spacing = s.column_spacing,
                                             .background     = Gray,
                                             .width          = 240.0f,
                                             .height         = 160.0f,
                                             .content =
                                                 {
                                                     ui::Cell{.row     = ui::bind(s.row, to_index),
                                                              .column  = ui::bind(s.column, to_index),
                                                              .content = rect(Red, 50.0f, 50.0f)},
                                                     ui::Cell{.row = 1, .column = 0, .content = rect(Blue, 50.0f, 50.0f)},
                                                     ui::Cell{.row = 0, .column = 1, .content = rect(Green, 50.0f, 50.0f)},
                                                     ui::Cell{.row = 1, .column = 1, .content = rect(Yellow, 50.0f, 50.0f)},
                                                 }},
                                },
                        }),
                });
        });
}

// ---- LayoutPanel ----

// The sample's custom layout: the first child across the whole width, the
// rest two to a row, each row as tall as its taller child.
class FeaturedTileLayout final : public ui::NonVirtualizingLayout
{
public:
    explicit FeaturedTileLayout(float spacing) : spacing_(spacing)
    {
    }

    void set_spacing(float spacing)
    {
        spacing_ = spacing;
        invalidate_measure();
    }

    ui::Size measure(ui::NonVirtualizingLayoutContext& context, ui::Size available) override
    {
        const float width  = available.width == std::numeric_limits<float>::infinity() ? 520.0f : available.width;
        const float column = std::max(0.0f, (width - spacing_) / 2.0f);
        for (std::size_t i = 0; i < context.item_count(); ++i)
        {
            (void)context.measure(i, ui::Size{i == 0 ? width : column, std::numeric_limits<float>::infinity()});
        }
        float height = context.item_count() == 0 ? 0.0f : context.desired_size(0).height;
        for (std::size_t i = 1; i < context.item_count(); i += 2)
        {
            height += spacing_ + row_height(context, i);
        }
        return ui::Size{width, height};
    }

    ui::Size arrange(ui::NonVirtualizingLayoutContext& context, ui::Size final_size) override
    {
        if (context.item_count() == 0)
        {
            return final_size;
        }
        const float column = std::max(0.0f, (final_size.width - spacing_) / 2.0f);
        context.arrange(0, ui::Rect{0.0f, 0.0f, final_size.width, context.desired_size(0).height});
        float y = context.desired_size(0).height;
        for (std::size_t i = 1; i < context.item_count(); i += 2)
        {
            y += spacing_;
            const float height = row_height(context, i);
            context.arrange(i, ui::Rect{0.0f, y, column, height});
            if (i + 1 < context.item_count())
            {
                context.arrange(i + 1, ui::Rect{column + spacing_, y, column, height});
            }
            y += height;
        }
        return ui::Size{final_size.width, y};
    }

private:
    static float row_height(ui::NonVirtualizingLayoutContext& context, std::size_t first)
    {
        const float height = context.desired_size(first).height;
        return first + 1 < context.item_count() ? std::max(height, context.desired_size(first + 1).height) : height;
    }

    float spacing_;
};

struct LayoutPanelState
{
    ui::Control<ui::LayoutPanel> panel;
    float                        spacing = 8.0f;
    bool                         stack   = false;

    void apply()
    {
        if (panel == nullptr)
        {
            return;
        }
        if (stack)
        {
            panel->set_layout(std::make_unique<ui::StackLayoutEngine>())->set_spacing(spacing);
        }
        else
        {
            panel->set_layout(std::make_unique<FeaturedTileLayout>(spacing));
        }
    }
};

ui::Spec tile(const char* text, float height, ui::TextRole role = ui::TextRole::Body)
{
    return themed_box(BoxStyle::Control,
                      ui::Thickness{12.0f},
                      ui::VerticalStack{.height  = height - 24.0f,
                                        .content = {ui::Text{.text                 = text,
                                                             .role                 = role,
                                                             .horizontal_alignment = ui::Alignment::Leading,
                                                             .vertical_alignment   = ui::Alignment::Center}}});
}

// The sample's LayoutPanel at most 560 wide, on the frame that stands in for its border.
std::unique_ptr<ui::Element> capped(float max_width, std::unique_ptr<ui::Element> element)
{
    element->set_max_width(max_width);
    element->set_horizontal_alignment(ui::Alignment::Stretch);
    return element;
}

ui::Spec layout_panel_page(Context&)
{
    return with_state<LayoutPanelState>(
        [](LayoutPanelState& s)
        {
            auto page = examples(
                {
                    example({
                        .title   = "Use a custom Layout with panel children",
                        .code    = R"cpp(
                            // The app's own layout: the first child across the whole width,
                            // the rest two to a row.
                            class FeaturedTileLayout final : public ui::NonVirtualizingLayout
                            {
                            public:
                                explicit FeaturedTileLayout(float spacing);
                                ui::Size measure(ui::NonVirtualizingLayoutContext& context, ui::Size available) override;
                                ui::Size arrange(ui::NonVirtualizingLayoutContext& context, ui::Size final_size) override;
                            };

                            ui::Control<ui::LayoutPanel> panel;

                            ui::LayoutPanel{
                                .layout  = std::make_unique<FeaturedTileLayout>(8.0f),
                                .padding = ui::Thickness{12.0f},
                                .control = panel,
                                .content = {
                                    ui::Border{.padding = ui::Thickness{16.0f}, .content = ui::Text{.text = "Featured item"}},
                                    ui::Border{.padding = ui::Thickness{12.0f}, .content = ui::Text{.text = "Second"}},
                                    ui::Border{.padding = ui::Thickness{12.0f}, .content = ui::Text{.text = "Third (taller)"}},
                                    // ... six more tiles ...
                                },
                            }

                            // Swap the layout while the panel is shown; set_layout() returns the new one.
                            panel->set_layout(std::make_unique<ui::StackLayoutEngine>())->set_spacing(8.0f);
                            panel->set_layout(std::make_unique<FeaturedTileLayout>(8.0f));
                        )cpp",
                        .stretch = true,
                        .options = options_panel(220.0f,
                                                 {
                                                     ui::RadioButtons{.header = "Layout",
                                                                      .items  = {"Featured tile", "Vertical stack"},
                                                                      .selected_index = 0,
                                                                      .on_selection_changed =
                                                                          [&s](std::optional<std::size_t> i)
                                                                      {
                                                                          s.stack = i == 1;
                                                                          s.apply();
                                                                      }},
                                                     ui::Slider{.header         = "Spacing",
                                                                .value          = 8.0f,
                                                                .max            = 24.0f,
                                                                .tick_frequency = 2.0f,
                                                                .step_frequency = 2.0f,
                                                                .snaps_to       = ui::SliderSnapsTo::Ticks,
                                                                .on_value_changed =
                                                                    [&s](double v)
                                                                {
                                                                    s.spacing = static_cast<float>(static_cast<float>(v));
                                                                    if (s.panel == nullptr)
                                                                    {
                                                                        return;
                                                                    }
                                                                    if (auto* stack = dynamic_cast<ui::StackLayoutEngine*>(
                                                                            s.panel->layout()))
                                                                    {
                                                                        stack->set_spacing(static_cast<float>(v));
                                                                    }
                                                                    else if (auto* tiles = dynamic_cast<FeaturedTileLayout*>(
                                                                                 s.panel->layout()))
                                                                    {
                                                                        tiles->set_spacing(static_cast<float>(v));
                                                                    }
                                                                }},
                                                     ui::Text{.text     = "LayoutPanel also applies its border brush, "
                                                                          "border thickness, corner radius "
                                                                          "and padding around the layout.",
                                                              .wrapping = ui::TextWrapping::Wrap,
                                                              .horizontal_alignment = ui::Alignment::Leading},
                                                 }),
                        // The panel's border is the theme's card stroke, which a LayoutPanel's plain
                        // border brush cannot follow: the frame around it draws it.
                        .controls =
                            {
                                capped(
                                    560.0f,
                                    themed_box(BoxStyle::Frame,
                                               ui::Thickness{0.0f},
                                               ui::LayoutPanel{.layout  = std::make_unique<FeaturedTileLayout>(8.0f),
                                                               .padding = ui::Thickness{12.0f},
                                                               .control = s.panel,
                                                               .content =
                                                                   {
                                                                       themed_box(BoxStyle::Accent,
                                                                                  ui::Thickness{16.0f},
                                                                                  ui::VerticalStack{.height = 64.0f,
                                                                                                    .vertical_alignment = ui::Alignment::Center,
                                                                                                    .content =
                                                                                                        {ui::Text{.text = "Featured item",
                                                                                                                  .role = ui::TextRole::BodyStrong,
                                                                                                                  .horizontal_alignment = ui::Alignment::Leading},
                                                                                                         ui::Text{.text = "The first child spans the available width.",
                                                                                                                  .horizontal_alignment = ui::Alignment::Leading}}}),
                                                                       tile("Second", 72.0f),
                                                                       tile("Third (taller)", 92.0f),
                                                                       tile("Fourth", 80.0f),
                                                                       tile("Fifth", 64.0f),
                                                                       tile("Sixth (taller)", 88.0f),
                                                                       tile("Seventh", 68.0f),
                                                                       tile("Eighth", 76.0f),
                                                                       tile("Ninth (taller)", 96.0f),
                                                                   }})),
                            },
                    }),
                });
            return page;
        });
}

// ---- RelativePanel ----

ui::Spec relative_panel_page(Context&)
{
    return examples(
        {
            example(
                {
                    .title = "A RelativePanel control.",
                    .code  = R"cpp(
                        const ui::Color red    = ui::Color::from_argb(0xFFFF0000);
                        const ui::Color blue   = ui::Color::from_argb(0xFF0000FF);
                        const ui::Color green  = ui::Color::from_argb(0xFF008000);
                        const ui::Color yellow = ui::Color::from_argb(0xFFFFFF00);

                        ui::RelativePanel{
                            .width   = 300,
                            .content = {
                                ui::RelativeItem{.name = "Rectangle1", .content = ui::Rectangle{.fill = red, .width = 50, .height = 50}},
                                ui::RelativeItem{.name     = "Rectangle2",
                                 .right_of = "Rectangle1",
                                 .content  = ui::Rectangle{.fill   = blue,
                                                           .margin = ui::Edges{.left = 8.0f},
                                                           .width  = 50,
                                                           .height = 50}},
                                ui::RelativeItem{.name                   = "Rectangle3",
                                 .align_right_with_panel = true,
                                 .content                = ui::Rectangle{.fill = green, .width = 50, .height = 50}},
                                ui::RelativeItem{.name                         = "Rectangle4",
                                 .below                        = "Rectangle3",
                                 .align_horizontal_center_with = "Rectangle3",
                                 .content = ui::Rectangle{.fill   = yellow,
                                                          .margin = ui::Edges{.top = 8.0f},
                                                          .width  = 50,
                                                          .height = 50}},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::RelativePanel{
                                .width = 300.0f,
                                .content =
                                    {
                                        ui::RelativeItem{.name = "Rectangle1", .content = rect(Red, 50.0f, 50.0f)},
                                        ui::RelativeItem{.name     = "Rectangle2",
                                                         .right_of = "Rectangle1",
                                                         .content = ui::VerticalStack{.margin = ui::Edges{.left = 8.0f},
                                                                                      .content = {rect(Blue, 50.0f, 50.0f)}}},
                                        ui::RelativeItem{.name                   = "Rectangle3",
                                                         .align_right_with_panel = true,
                                                         .content                = rect(Green, 50.0f, 50.0f)},
                                        ui::RelativeItem{.name                         = "Rectangle4",
                                                         .below                        = "Rectangle3",
                                                         .align_horizontal_center_with = "Rectangle3",
                                                         .content = ui::VerticalStack{.margin = ui::Edges{.top = 8.0f},
                                                                                      .content = {rect(Yellow, 50.0f, 50.0f)}}},
                                    }},
                        },
                }),
        });
}

// ---- SidePane ----

struct SidePaneState
{
    ui::Property<bool>                     open{true};
    ui::Property<ui::SidePaneDisplayMode>  mode{ui::SidePaneDisplayMode::Inline};
    ui::Property<ui::SidePanePlacement>    placement{ui::SidePanePlacement::Left};
    ui::Property<std::optional<ui::Color>> background{std::nullopt};
    ui::Property<float>                    open_length{256.0f};
    ui::Property<float>                    compact_length{48.0f};
    ui::Property<std::string>              content{""};
};

ui::Spec split_view_page(Context&)
{
    return with_state<SidePaneState>(
        [](SidePaneState& s)
        {
            static const std::array<const char*, 4>          Links{"People", "Globe", "Message", "Mail"};
            constexpr std::array<ui::SidePaneDisplayMode, 4> Modes{ui::SidePaneDisplayMode::Inline,
                                                                   ui::SidePaneDisplayMode::CompactInline,
                                                                   ui::SidePaneDisplayMode::Overlay,
                                                                   ui::SidePaneDisplayMode::CompactOverlay};
            return examples({
                example({
                    .title = "A basic SidePane.",
                    .code  = R"cpp(
                        ui::Property<bool>                       open{true};
                        ui::Property<ui::SidePaneDisplayMode>   mode{ui::SidePaneDisplayMode::Inline};
                        ui::Property<ui::SidePanePlacement> placement{ui::SidePanePlacement::Left};
                        ui::Property<std::optional<ui::Color>>   background{std::nullopt};
                        ui::Property<float>                      open_length{256.0f};
                        ui::Property<float>                      compact_length{48.0f};
                        ui::Property<std::string>                page{""};

                        ui::SidePane{
                            .is_pane_open        = open,
                            .display_mode        = mode,
                            .pane_placement      = placement,
                            .open_pane_length    = open_length,
                            .compact_pane_length = compact_length,
                            .pane_background     = background,
                            .width               = 400,
                            .height              = 300,
                            // A light dismiss closes the pane without writing the binding.
                            .on_pane_closed = [&] { open = false; },
                            .pane = ui::VerticalStack{
                                .content = {
                                    ui::Text{.text = "PANE CONTENT", .role = ui::TextRole::BodyStrong},
                                    ui::ListView<std::string>{
                                        .items                = {"People", "Globe", "Message", "Mail"},
                                        .on_selection_changed = [&](std::optional<std::string> link)
                                        {
                                            if (link)
                                            {
                                                page = *link + " Page";
                                            }
                                        },
                                    },
                                },
                            },
                            .content = ui::VerticalStack{
                                .content = {
                                    ui::Text{.text = "SPLITVIEW CONTENT", .role = ui::TextRole::BodyStrong},
                                    ui::Text{.text = page},
                                },
                            },
                        }
                    )cpp",
                    .options =
                        options_panel(
                            196.0f,
                            {
                                ui::ToggleButton{.text    = "Open pane",
                                                 .checked = ui::bind_one_way(s.open),
                                                 .horizontal_alignment = ui::Alignment::Leading,
                                                 .on_checked_changed = [&s](std::optional<bool> on) { s.open = on == true; }},
                                ui::ToggleSwitch{.header      = "Placement",
                                                 .checked_text  = "Right",
                                                 .unchecked_text = "Left",
                                                 .margin      = ui::Edges{.top = 12.0f},
                                                 .min_width   = 120.0f,
                                                 .on_checked_changed  = [&s](bool on)
                                                 { s.placement = on ? ui::SidePanePlacement::Right : ui::SidePanePlacement::Left; }},
                                ui::ComboBox{.header         = "Display mode",
                                             .items          = {"Inline", "CompactInline", "Overlay", "CompactOverlay"},
                                             .selected_index = 0,
                                             .on_selection_changed =
                                                 [&s, Modes](std::optional<std::size_t> i)
                                             {
                                                 if (i.value_or(0) < Modes.size())
                                                 {
                                                     s.mode = Modes[i.value_or(0)];
                                                 }
                                             }},
                                ui::ComboBox{.header         = "Pane background",
                                             .items          = {"Theme default", "Red", "Blue", "Green"},
                                             .selected_index = 0,
                                             .on_selection_changed =
                                                 [&s](std::optional<std::size_t> i)
                                             {
                                                 constexpr std::array<ui::Color, 3> Fills{Red, Blue, Green};
                                                 s.background = i == 0 || i.value_or(0) > Fills.size() ? std::optional<ui::Color>{}
                                                                                            : std::optional<ui::Color>{Fills[i.value_or(0) - 1]};
                                             }},
                                ui::Slider{.header           = "Open pane length",
                                           .value            = s.open_length,
                                           .min              = 128.0f,
                                           .max              = 500.0f,
                                           .step_frequency   = 8.0f},
                                ui::Slider{.header           = "Compact pane length",
                                           .value            = s.compact_length,
                                           .min              = 24.0f,
                                           .max              = 128.0f,
                                           .step_frequency   = 8.0f},
                            }),
                    .controls =
                        {
                            ui::SidePane{
                                .is_pane_open        = s.open,
                                .display_mode        = s.mode,
                                .pane_placement      = s.placement,
                                .open_pane_length    = s.open_length,
                                .compact_pane_length = s.compact_length,
                                .pane_background     = s.background,
                                .width               = 400.0f,
                                .height              = 300.0f,
                                .max_width           = 400.0f,
                                .vertical_alignment             = ui::Alignment::Leading,
                                // A light dismiss closes the pane behind the binding's back:
                                // tell the toggle button.
                                .on_pane_closed = [&s] { s.open = false; },
                                .pane =
                                    ui::Grid{.columns = {1_fr},
                                             .rows    = {ui::AutoSize, 1_fr},
                                             .content =
                                                 {
                                                     ui::Cell{.row     = 0,
                                                      .column  = 0,
                                                      .content = ui::Text{.text    = "PANE CONTENT",
                                                                          .role    = ui::TextRole::BodyStrong,
                                                                          .margin  = ui::Edges{.left = 60.0f, .top = 12.0f},
                                                                          .horizontal_alignment = ui::Alignment::Leading}},
                                                     ui::Cell{.row     = 1,
                                                      .column  = 0,
                                                      .content = ui::ListView<std::string>{
                                                          .items  = {Links[0], Links[1], Links[2], Links[3]},
                                                          .margin = ui::Edges{.top = 12.0f},
                                                          .on_selection_changed =
                                                              [&s](std::optional<std::string> link)
                                                          {
                                                              if (link)
                                                              {
                                                                  s.content = *link + " Page";
                                                              }
                                                          }}},
                                                 }},
                                .content =
                                    ui::Grid{.columns = {1_fr},
                                             .rows    = {ui::AutoSize, 1_fr},
                                             .content =
                                                 {
                                                     ui::Cell{.row     = 0,
                                                      .column  = 0,
                                                      // Wrapped: beside the 256px
                                                      // pane only 132px are left, and a clipped "T" looks like a bug.
                                                      .content = ui::Text{.text     = "SPLITVIEW CONTENT",
                                                                          .role     = ui::TextRole::BodyStrong,
                                                                          .wrapping = ui::TextWrapping::WrapWholeWords,
                                                                          .margin   = ui::Edges{.left = 12.0f, .top = 12.0f},
                                                                          .horizontal_alignment  = ui::Alignment::Leading}},
                                                     ui::Cell{.row     = 1,
                                                      .column  = 0,
                                                      .content = ui::Text{.text    = s.content,
                                                                          .margin  = ui::Edges{.left = 12.0f, .top = 12.0f},
                                                                          .horizontal_alignment = ui::Alignment::Leading,
                                                                          .vertical_alignment = ui::Alignment::Leading}},
                                                 }},
                            },
                        },
                }),
            });
        });
}

// ---- Stack ----

struct StackState
{
    ui::Property<float>            spacing{8.0f};
    ui::Control<ui::VerticalStack> stack;
    ui::Property<float>            padding{12.0f};
    ui::Property<float>            corner{8.0f};
};

// The second card, named so the page's list of cards stays shallow for the compiler.
ui::Spec boxed_stack_example(StackState& s)
{
    ui::Spec stack = ui::HorizontalStack{.spacing          = 8.0f,
                                         .border_brush     = [](const ui::Theme& t) { return t.colors().divider; },
                                         .border_thickness = ui::Thickness{1.0f},
                                         .corner_radius    = s.corner,
                                         .padding = ui::bind(s.padding, [](float p) { return ui::Thickness{p}; }),
                                         .horizontal_alignment = ui::Alignment::Leading,
                                         .content              = {rect(Red), rect(Blue), rect(Green)}};
    return example({
        .title    = "A stack with a border and padding.",
        .code     = R"cpp(
            ui::Property<float> padding{12.0f};
            ui::Property<float> corner{8.0f};

            ui::HorizontalStack{
                .spacing          = 8,
                .border_brush     = [](const ui::Theme& t) { return t.colors().divider; },
                .border_thickness = ui::Thickness{1},
                .corner_radius    = corner,
                .padding          = ui::bind(padding, [](float p) { return ui::Thickness{p}; }),
                .content          = {/* ... three ui::Rectangle ... */},
            }
        )cpp",
        .options  = options_panel(180.0f,
                                  {
                                      horizontal_slider("Padding", 0.0f, 32.0f, s.padding, 4.0f),
                                      horizontal_slider("Corner radius", 0.0f, 16.0f, s.corner, 2.0f),
                                  }),
        .controls = {std::move(stack)},
    });
}

ui::Spec stack_page(Context&)
{
    return with_state<StackState>(
        [](StackState& s)
        {
            return examples({
                example({
                    .title   = "A Stack control.",
                    .code    = R"cpp(
                        const ui::Color red    = ui::Color::from_argb(0xFFFF0000);
                        const ui::Color blue   = ui::Color::from_argb(0xFF0000FF);
                        const ui::Color green  = ui::Color::from_argb(0xFF008000);
                        const ui::Color yellow = ui::Color::from_argb(0xFFFFFF00);

                        ui::Property<float>            spacing{8.0f};
                        ui::Control<ui::VerticalStack> stack;

                        ui::VerticalStack{
                            .spacing            = spacing,
                            .vertical_alignment = ui::Alignment::Leading,
                            .control            = stack,
                            .content            = {
                                ui::Rectangle{.fill = red, .width = 40, .height = 40},
                                ui::Rectangle{.fill = blue, .width = 40, .height = 40},
                                ui::Rectangle{.fill = green, .width = 40, .height = 40},
                                ui::Rectangle{.fill = yellow, .width = 40, .height = 40},
                            },
                        }

                        // Both stacks build a ui::StackControl, which can be turned later.
                        stack->set_orientation(ui::Orientation::Horizontal);
                    )cpp",
                    .options = options_panel(180.0f,
                                             {
                                                 ui::RadioButtons{.header         = "Orientation",
                                                                  .items          = {"Horizontal", "Vertical"},
                                                                  .selected_index = 1,
                                                                  .on_selection_changed =
                                                                      [&s](std::optional<std::size_t> i)
                                                                  {
                                                                      if (s.stack != nullptr)
                                                                      {
                                                                          s.stack->set_orientation(
                                                                              i == 0 ? ui::Orientation::Horizontal
                                                                                     : ui::Orientation::Vertical);
                                                                      }
                                                                  }},
                                                 horizontal_slider("Spacing", 0.0f, 16.0f, s.spacing, 1.0f),
                                             }),
                    .controls =
                        {
                            ui::VerticalStack{.spacing            = s.spacing,
                                              .vertical_alignment = ui::Alignment::Leading,
                                              .control            = s.stack,
                                              .content            = {rect(Red), rect(Blue), rect(Green), rect(Yellow)}},
                        },
                }),
                boxed_stack_example(s),
            });
        });
}

// ---- TilePanel ----

struct TilePanelState
{
    ui::Property<ui::Orientation> orientation{ui::Orientation::Vertical};
};

ui::Spec tile_panel_page(Context&)
{
    return with_state<TilePanelState>(
        [](TilePanelState& s)
        {
            return examples({
                example({
                    .title   = "A TilePanel control.",
                    .code    = R"cpp(
                        const ui::Color red    = ui::Color::from_argb(0xFFFF0000);
                        const ui::Color blue   = ui::Color::from_argb(0xFF0000FF);
                        const ui::Color green  = ui::Color::from_argb(0xFF008000);
                        const ui::Color yellow = ui::Color::from_argb(0xFFFFFF00);

                        ui::Property<ui::Orientation> orientation{ui::Orientation::Vertical};

                        ui::TilePanel{
                            .orientation             = orientation,
                            .item_width              = 44,
                            .item_height             = 44,
                            .maximum_rows_or_columns = 3,
                            .width                   = 400,
                            .content                 = {
                                ui::TilePanelItem{.content = ui::Rectangle{.fill = red, .width = 40, .height = 40}},
                                ui::TilePanelItem{.row_span = 2, .content = ui::Rectangle{.fill = blue, .width = 40, .height = 80}},
                                ui::TilePanelItem{.column_span = 2, .content = ui::Rectangle{.fill = green, .width = 80, .height = 40}},
                                ui::TilePanelItem{.row_span = 2, .column_span = 2, .content = ui::Rectangle{.fill = yellow, .width = 80, .height = 80}},
                            },
                        }
                    )cpp",
                    .options = ui::RadioButtons{.header         = "Orientation",
                                                .items          = {"Horizontal", "Vertical"},
                                                .selected_index = 1,
                                                .on_selection_changed =
                                                    [&s](std::optional<std::size_t> i)
                                                {
                                                    s.orientation = i == 0 ? ui::Orientation::Horizontal
                                                                           : ui::Orientation::Vertical;
                                                }},
                    .controls =
                        {
                            ui::TilePanel{.orientation             = s.orientation,
                                          .item_width              = 44.0f,
                                          .item_height             = 44.0f,
                                          .maximum_rows_or_columns = 3,
                                          .width                   = 400.0f,
                                          .content =
                                              {
                                                  ui::TilePanelItem{.content = rect(Red, 0.0f, 0.0f)},
                                                  ui::TilePanelItem{.row_span = 2, .content = rect(Blue, 0.0f, 80.0f)},
                                                  ui::TilePanelItem{.column_span = 2, .content = rect(Green, 80.0f, 0.0f)},
                                                  ui::TilePanelItem{.row_span    = 2,
                                                                    .column_span = 2,
                                                                    .content     = rect(Yellow, 80.0f, 80.0f)},
                                              }},
                        },
                }),
            });
        });
}

// ---- WrapPanel ----

struct WrapPanelState
{
    ui::Property<ui::Orientation> orientation{ui::Orientation::Horizontal};
    ui::Property<float>           item_spacing{8.0f};
    ui::Property<float>           line_spacing{8.0f};
    ui::Property<float>           padding{12.0f};
    ui::Property<bool>            stretch_last{true};
    ui::Element*                  host = nullptr;
};

ui::Spec chip(const char* text, float min_width, float width = 0.0f)
{
    auto box = themed_box(BoxStyle::Control,
                          ui::Symmetric{.horizontal = 12.0f, .vertical = 8.0f},
                          ui::Text{.text = text, .horizontal_alignment = ui::Alignment::Leading});
    box->set_min_width(min_width);
    if (width > 0.0f)
    {
        box->set_width(width);
    }
    return box;
}

ui::Spec wrap_panel_page(Context&)
{
    return with_state<WrapPanelState>(
        [](WrapPanelState& s)
        {
            auto spacing_slider = [](const char* header, float value, ui::Property<float>& target)
            {
                return ui::Slider{.header           = header,
                                  .value            = value,
                                  .max              = 24.0f,
                                  .tick_frequency   = 2.0f,
                                  .step_frequency   = 2.0f,
                                  .snaps_to         = ui::SliderSnapsTo::Ticks,
                                  .on_value_changed = [&target](double v)
                                  { target = static_cast<float>(static_cast<float>(v)); }};
            };

            auto host = themed_box(BoxStyle::Card,
                                   ui::Thickness{0.0f},
                                   ui::WrapPanel{.orientation  = s.orientation,
                                                 .item_spacing = s.item_spacing,
                                                 .line_spacing = s.line_spacing,
                                                 .padding = ui::bind(s.padding, [](float p) { return ui::Thickness{p}; }),
                                                 .content = {chip("Alpha", 72.0f),
                                                             chip("Beta item", 112.0f),
                                                             chip("Gamma", 88.0f),
                                                             chip("Delta item", 128.0f),
                                                             chip("Epsilon", 80.0f),
                                                             chip("Zeta item", 104.0f),
                                                             chip("Eta", 76.0f),
                                                             chip("Theta item", 120.0f),
                                                             chip("Iota", 92.0f),
                                                             chip("Kappa item", 132.0f)}});
            host->set_width(360.0f);
            host->set_height(200.0f);
            host->set_horizontal_alignment(ui::Alignment::Leading);
            host->set_vertical_alignment(ui::Alignment::Leading);
            s.host = host.get();

            auto final_item   = themed_box(BoxStyle::Accent,
                                           ui::Symmetric{.horizontal = 12.0f, .vertical = 8.0f},
                                           ui::Text{.text = "Final item"});
            auto stretch_host = themed_box(BoxStyle::Card,
                                           ui::Thickness{0.0f},
                                           ui::WrapPanel{.item_spacing  = 8.0f,
                                                         .items_stretch = ui::bind(s.stretch_last,
                                                                                   [](bool on)
                                                                                   {
                                                                                       return on ? ui::WrapPanelItemsStretch::Last
                                                                                                 : ui::WrapPanelItemsStretch::None;
                                                                                   }),
                                                         .padding       = ui::Thickness{12.0f},
                                                         .content       = {chip("One", 0.0f, 72.0f),
                                                                           chip("Two", 0.0f, 72.0f),
                                                                           chip("Three", 0.0f, 72.0f),
                                                                           chip("Four", 0.0f, 72.0f),
                                                                           chip("Five", 0.0f, 72.0f),
                                                                           std::move(final_item)}});
            stretch_host->set_width(360.0f);
            stretch_host->set_horizontal_alignment(ui::Alignment::Leading);

            return examples({
                example({
                    .title    = "Wrap items and adjust the layout",
                    .code     = R"cpp(
                        ui::Property<ui::Orientation> orientation{ui::Orientation::Horizontal};
                        ui::Property<float>           item_spacing{8.0f};
                        ui::Property<float>           line_spacing{8.0f};
                        ui::Property<float>           padding{12.0f};

                        // One item: a padded box around its text.
                        auto chip = [](const char* text, float min_width) -> ui::Spec
                        {
                            return ui::Border{
                                .padding   = ui::Symmetric{.horizontal = 12.0f, .vertical = 8.0f},
                                .min_width = min_width,
                                .content   = ui::Text{.text = text},
                            };
                        };

                        ui::WrapPanel{
                            .orientation  = orientation,
                            .item_spacing = item_spacing,
                            .line_spacing = line_spacing,
                            .padding      = ui::bind(padding, [](float p) { return ui::Thickness{p}; }),
                            .width        = 360,
                            .height       = 200,
                            .content      = {
                                chip("Alpha", 72.0f),
                                chip("Beta item", 112.0f),
                                chip("Gamma", 88.0f),
                                chip("Delta item", 128.0f),
                                // ... six more ...
                            },
                        }
                    )cpp",
                    .options  = options_panel(180.0f,
                                              {
                                                  ui::RadioButtons{.header         = "Orientation",
                                                                   .items          = {"Horizontal", "Vertical"},
                                                                   .selected_index = 0,
                                                                   .on_selection_changed =
                                                                       [&s](std::optional<std::size_t> i)
                                                                   {
                                                                      const bool vertical = i == 1;
                                                                      s.orientation = vertical
                                                                                          ? ui::Orientation::Vertical
                                                                                          : ui::Orientation::Horizontal;
                                                                      // As the WrapPanel sample: a vertical panel gets a taller host.
                                                                      if (s.host != nullptr)
                                                                      {
                                                                          s.host->set_height(vertical ? 360.0f : 200.0f);
                                                                      }
                                                                   }},
                                                  spacing_slider("Item spacing", 8.0f, s.item_spacing),
                                                  spacing_slider("Line spacing", 8.0f, s.line_spacing),
                                                  spacing_slider("Padding", 12.0f, s.padding),
                                              }),
                    .controls = {std::move(host)},
                }),
                example({
                    .title    = "Stretch the final item to fill the line",
                    .code     = R"cpp(
                        ui::Property<bool> stretch_last{true};

                        ui::WrapPanel{
                            .item_spacing  = 8,
                            .items_stretch = ui::bind(stretch_last,
                                                      [](bool on)
                                                      {
                                                          return on ? ui::WrapPanelItemsStretch::Last
                                                                    : ui::WrapPanelItemsStretch::None;
                                                      }),
                            .padding       = ui::Thickness{12.0f},
                            .width         = 360,
                            .content       = {
                                ui::Border{.width = 72, .content = ui::Text{.text = "One"}},
                                ui::Border{.width = 72, .content = ui::Text{.text = "Two"}},
                                // ... Three, Four, Five ...
                                ui::Border{.content = ui::Text{.text = "Final item"}},
                            },
                        }
                    )cpp",
                    .options  = ui::ToggleSwitch{.header             = "Stretch items",
                                                 .checked            = true,
                                                 .checked_text       = "Last",
                                                 .unchecked_text     = "None",
                                                 .on_checked_changed = [&s](bool on) { s.stretch_last = on; }},
                    .controls = {std::move(stretch_host)},
                }),
            });
        });
}

// ---- ScaleToFit ----

struct ScaleToFitState
{
    ui::Property<float>                size{200.0f};
    ui::Property<ui::Stretch>          stretch{ui::Stretch::Uniform};
    ui::Property<ui::StretchDirection> direction{ui::StretchDirection::Both};
};

ui::Spec viewbox_page(Context&)
{
    return with_state<ScaleToFitState>(
        [](ScaleToFitState& s)
        {
            constexpr std::array<ui::Stretch, 4>
                Stretches{ui::Stretch::None, ui::Stretch::Fill, ui::Stretch::Uniform, ui::Stretch::UniformToFill};
            constexpr std::array<ui::StretchDirection, 3> Directions{ui::StretchDirection::UpOnly,
                                                                     ui::StretchDirection::DownOnly,
                                                                     ui::StretchDirection::Both};
            auto                                          page = examples({
                example({
                    .title   = "Content inside of a ScaleToFit.",
                    .code    = R"cpp(
                        ui::Property<float>                size{200.0f};
                        ui::Property<ui::Stretch>          stretch{ui::Stretch::Uniform};
                        ui::Property<ui::StretchDirection> direction{ui::StretchDirection::Both};

                        ui::ScaleToFit{
                            .stretch            = stretch,
                            .stretch_direction  = direction,
                            .width              = size,
                            .height             = size,
                            .vertical_alignment = ui::Alignment::Leading,
                            .content            = ui::Border{
                                .border_brush     = ui::Color::from_argb(0xFF808080),
                                .border_thickness = ui::Thickness{15.0f},
                                .content          = ui::VerticalStack{
                                    .background = ui::Color::from_argb(0xFFA9A9A9),
                                    .content    = {
                                        // ... a row of four colored rectangles ...
                                        ui::Image{.source = photo, .horizontal_alignment = ui::Alignment::Center},
                                        ui::Text{.text = "This is text.", .horizontal_alignment = ui::Alignment::Center},
                                    },
                                },
                            },
                        }
                    )cpp",
                    .options = options_panel(
                        200.0f,
                        {
                            ui::Slider{.header = "Width/Height", .value = s.size, .min = 20.0f, .max = 300.0f},
                            ui::RadioButtons{.header         = "Stretch",
                                             .items          = {"None", "Fill", "Uniform", "UniformToFill"},
                                             .selected_index = 2,
                                             .on_selection_changed =
                                                 [&s, Stretches](std::optional<std::size_t> i)
                                             {
                                                 if (i.value_or(0) < 4)
                                                 {
                                                     s.stretch = Stretches[static_cast<std::size_t>(i.value_or(0))];
                                                 }
                                             }},
                            ui::RadioButtons{.header         = "Stretch direction",
                                             .items          = {"UpOnly", "DownOnly", "Both"},
                                             .selected_index = 2,
                                             .on_selection_changed =
                                                 [&s, Directions](std::optional<std::size_t> i)
                                             {
                                                 if (i.value_or(0) < 3)
                                                 {
                                                     s.direction = Directions[static_cast<std::size_t>(i.value_or(0))];
                                                 }
                                             }},
                        }),
                    .controls =
                        {
                            ui::ScaleToFit{.stretch            = s.stretch,
                                           .stretch_direction  = s.direction,
                                           .width              = s.size,
                                           .height             = s.size,
                                           .vertical_alignment = ui::Alignment::Leading,
                                           .content            = ui::
                                               Border{.border_brush     = Gray,
                                                      .border_thickness = ui::Thickness{15.0f},
                                                      .content = ui::VerticalStack{.background = DarkGray,
                                                                                   .content =
                                                                                       {
                                                                                           ui::HorizontalStack{
                                                                                               .content =
                                                                                                   {rect(Blue, 40.0f, 10.0f),
                                                                                                    rect(Green, 40.0f, 10.0f),
                                                                                                    rect(Red, 40.0f, 10.0f),
                                                                                                    rect(Yellow, 40.0f, 10.0f)}},
                                                                                           ui::Image{.source = placeholder_photo(),
                                                                                                     .horizontal_alignment = ui::Alignment::Center},
                                                                                           ui::Text{
                                                                                               .text = "This is text.",
                                                                                               .horizontal_alignment = ui::Alignment::Center},
                                                                                       }}}},
                        },
                }),
            });
            return page;
        });
}

// ---- libwinui's own ----

// `.min_width` gives the two fraction columns something to share: a Stack row measures its
// children with unbounded width, so the Grid would otherwise shrink to the two buttons.
ui::Spec splitter_page(Context&)
{
    ui::Spec columns = ui::Grid{.columns   = {ui::minmax(80_dip, 1_fr), 6_dip, ui::minmax(80_dip, 1_fr)},
                                .rows      = {60_dip},
                                .min_width = 420.0f,
                                .content   = {
                                    ui::Cell{.column = 0, .content = ui::Button{.text = "Left"}},
                                    ui::Cell{.column  = 1,
                                             .content = ui::Splitter{.accessibility = {.name = "column splitter"}}},
                                    ui::Cell{.column = 2, .content = ui::Button{.text = "Right"}},
                                }};
    ui::Spec rows    = ui::Grid{.rows      = {ui::minmax(40_dip, 80_dip), 6_dip, 1_fr},
                                .height    = 200.0f,
                                .min_width = 420.0f,
                                .content   = {
                                    ui::Cell{.row = 0, .content = ui::Text{.text = "Top, between 40 and 80 DIPs tall"}},
                                    ui::Cell{.row     = 1,
                                             .content = ui::Splitter{.orientation   = ui::Orientation::Horizontal,
                                                                     .accessibility = {.name = "row splitter"}}},
                                    ui::Cell{.row = 2, .content = ui::Text{.text = "Bottom, the rest"}},
                                }};
    return examples({
        example("A divider that resizes the columns beside it",
                {std::move(columns)},
                false,
                R"cpp(
                    ui::Grid{
                        // Neither column gets narrower than 80 DIPs.
                        .columns   = {ui::minmax(80_dip, 1_fr), 6_dip, ui::minmax(80_dip, 1_fr)},
                        .rows      = {60_dip},
                        .min_width = 420,
                        .content   = {
                            ui::Cell{.column = 0, .content = ui::Button{.text = "Left"}},
                            ui::Cell{.column = 1, .content = ui::Splitter{}}, // drag it, or Tab to it and use the arrows
                            ui::Cell{.column = 2, .content = ui::Button{.text = "Right"}},
                        },
                    }
                )cpp"),
        example("A divider between rows",
                {std::move(rows)},
                false,
                R"cpp(
                    ui::Grid{
                        .rows    = {ui::minmax(40_dip, 80_dip), 6_dip, 1_fr},
                        .height  = 200,
                        .content = {
                            ui::Cell{.row = 0, .content = ui::Text{.text = "Top, between 40 and 80 DIPs tall"}},
                            ui::Cell{.row = 1, .content = ui::Splitter{.orientation = ui::Orientation::Horizontal}},
                            ui::Cell{.row = 2, .content = ui::Text{.text = "Bottom, the rest"}},
                        },
                    }
                )cpp"),
    });
}

ui::Spec resize_gripper_page(Context& ctx)
{
    auto&                          status = ctx.status;
    ui::Control<ui::Grid>          grid;
    ui::Control<ui::ResizeGripper> gripper;

    std::unique_ptr<ui::Element> node = ui::build(
        example("A corner grip that resizes its cell",
                {
                    ui::Grid{.columns = {160_dip, 16_dip},
                             .rows    = {60_dip, 16_dip},
                             .control = grid,
                             .content =
                                 {
                                     ui::Cell{.row = 0, .column = 0, .content = ui::Button{.text = "Resizable"}},
                                     ui::Cell{.row = 1, .column = 1, .content = ui::ResizeGripper{.control = gripper}},
                                 }},
                },
                false,
                R"cpp(
                    ui::Control<ui::Grid> grid;

                    ui::Grid{
                        .columns = {160_dip, 16_dip},
                        .rows    = {60_dip, 16_dip},
                        .control = grid,
                        .content   = {
                            ui::Cell{.row = 0, .column = 0, .content = ui::Button{.text = "Resizable"}},
                            ui::Cell{.row     = 1,
                             .column  = 1,
                             .content = ui::ResizeGripper{
                                 .on_drag_delta = [&](ui::Point delta)
                                 {
                                     grid->set_column_width(0, std::max(40.0f, grid->column_size(0) + delta.x));
                                     grid->set_row_height(0, std::max(24.0f, grid->row_size(0) + delta.y));
                                 },
                             }},
                        },
                    }
                )cpp"));

    gripper->on_drag_delta(
        [&status, grid](ui::Point delta)
        {
            grid->set_column_width(0, std::max(40.0f, grid->column_size(0) + delta.x));
            grid->set_row_height(0, std::max(24.0f, grid->row_size(0) + delta.y));
            status = "ResizeGripper: dragged";
        });
    return node;
}

// The min_wide_mode_width default (641) is wider than this pane, so both thresholds are lowered.
// The Dip-track Grid gives it a bounded offer, since the mode follows the offered extent.
ui::Spec two_pane_view_page(Context& ctx)
{
    auto& status = ctx.status;
    return example("Two panes that reflow with the space available",
                   {
                       ui::Grid{.columns = {400_dip},
                                .rows    = {60_dip},
                                .content =
                                    {
                                        ui::Cell{.row    = 0,
                                                 .column = 0,
                                                 .content =
                                                     ui::TwoPane{
                                                         .min_wide_mode_width  = 250.0f,
                                                         .min_tall_mode_height = 250.0f,
                                                         .pane1 = ui::Button{.text     = "Pane 1",
                                                                             .on_click = [&status]
                                                                             { status = "TwoPane: Pane 1 clicked"; }},
                                                         .pane2 = ui::Button{.text     = "Pane 2",
                                                                             .on_click = [&status]
                                                                             { status = "TwoPane: Pane 2 clicked"; }},
                                                     }},
                                    }},
                   },
                   false,
                   R"cpp(
                       // The mode follows the room the view is offered: the fixed tracks bound it,
                       // and both thresholds are lowered from their 641 default to fit.
                       ui::Grid{
                           .columns = {400_dip},
                           .rows    = {60_dip},
                           .content   = {
                               ui::Cell{.row     = 0,
                                .column  = 0,
                                .content = ui::TwoPane{
                                    .min_wide_mode_width  = 250,
                                    .min_tall_mode_height = 250,
                                    .pane1                = ui::Button{.text = "Pane 1", .on_click = [] { /* ... */ }},
                                    .pane2                = ui::Button{.text = "Pane 2", .on_click = [] { /* ... */ }},
                                }},
                           },
                       }
                   )cpp");
}

// ---- Match, Lazy, State -----------------------------------------------------------------------

struct MatchState
{
    ui::Property<bool>        advanced{false};
    ui::Property<bool>        details{false};
    ui::Property<int>         built{0};
    ui::Property<std::size_t> tab{0};
};

// A counter that owns its count: three of them on the page share nothing.
ui::Spec counter(std::string label)
{
    return ui::State<ui::Property<int>>{
        .content = [label = std::move(label)](ui::Property<int>& count) mutable -> ui::Spec
        {
            return ui::HorizontalStack{
                .spacing         = 8.0f,
                .child_alignment = ui::Alignment::Center,
                .content =
                    {
                        ui::Button{.text = std::move(label), .on_click = [&count] { ++count; }},
                        ui::Text{.text = ui::bind(count, [](int n) { return std::format("Clicked {} times", n); })},
                    },
            };
        },
    };
}

ui::Spec match_page(Context&)
{
    return with_state<MatchState>(
        [](MatchState& s)
        {
            const auto page = [](const std::size_t& index) -> ui::Spec
            {
                return ui::TextBox{
                    .header           = std::format("Page {}", index + 1),
                    .placeholder_text = "Type here, then switch",
                    .width            = 200.0f,
                };
            };

            // Named, not nested: a ui::Match deep in a braced list is expensive for MSVC to compile.
            ui::Spec rebuilt = ui::Match<std::size_t>{.value = s.tab, .content = page};
            ui::Spec kept    = ui::Match<std::size_t>{.value = s.tab, .keep = true, .content = page};

            return examples(
                {
                    example({
                        .title    = "visible hides a control and keeps what was typed",
                        .code     = R"cpp(
                        ui::Property<bool> advanced{false};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::ToggleSwitch{.checked = advanced},
                                ui::TextBox{
                                    .placeholder_text = "Type, hide, show again",
                                    .width            = 260,
                                    .visible          = advanced,
                                },
                            },
                        }
                    )cpp",
                        .controls = {ui::VerticalStack{
                            .spacing = 8.0f,
                            .content =
                                {
                                    ui::ToggleSwitch{.checked = s.advanced},
                                    ui::TextBox{
                                        .placeholder_text = "Type, hide, show again",
                                        .width            = 260.0f,
                                        .visible          = s.advanced,
                                    },
                                },
                        }},
                    }),
                    example(
                        {
                            .title = "Lazy builds its content the first time it shows",
                            .code  = R"cpp(
                        ui::Property<bool> details{false};
                        ui::Property<int>  built{0};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::ToggleSwitch{.checked = details},
                                ui::Text{.text = ui::bind(built, [](int n) { return std::format("Built {} times", n); })},
                                ui::Lazy{
                                    .visible = details,
                                    // Runs once, the first time details is true.
                                    .content = [&]() -> ui::Spec
                                    {
                                        ++built;
                                        return ui::TextBox{.placeholder_text = "Built on first use", .width = 260};
                                    },
                                },
                            },
                        }
                    )cpp",
                            .controls =
                                {
                                    ui::VerticalStack{
                                        .spacing = 8.0f,
                                        .content =
                                            {
                                                ui::ToggleSwitch{.checked = s.details},
                                                ui::Text{.text = ui::bind(s.built,
                                                                          [](int n)
                                                                          { return std::format("Built {} times", n); })},
                                                ui::Lazy{
                                                    .visible = s
                                                                   .details,
                                                    .content = [&s]() -> ui::Spec
                                                    {
                                                        ++s.built;
                                                        return ui::TextBox{.placeholder_text = "Built on first use",
                                                                           .width            = 260.0f};
                                                    },
                                                },
                                            },
                                    }},
                        }),
                    example({
                        .title = "Match rebuilds its content; with keep it returns to the page it built",
                        .code  = R"cpp(
                        ui::Property<std::size_t> tab{0};

                        const auto page = [](const std::size_t& index) -> ui::Spec
                        {
                            return ui::TextBox{
                                .header           = std::format("Page {}", index + 1),
                                .placeholder_text = "Type here, then switch",
                                .width            = 200,
                            };
                        };

                        ui::VerticalStack{
                            .spacing = 12,
                            .content = {
                                ui::SelectorBar{
                                    .items                = {"One", "Two", "Three"},
                                    .on_selection_changed = [&](std::size_t index) { tab = index; },
                                },
                                ui::HorizontalStack{
                                    .spacing = 24,
                                    .content = {
                                        // A new page each time: what was typed is gone.
                                        ui::Match<std::size_t>{.value = tab, .content = page},
                                        // One page per value, built once and kept.
                                        ui::Match<std::size_t>{.value = tab, .keep = true, .content = page},
                                    },
                                },
                            },
                        }
                    )cpp",
                        .controls = {ui::
                                         VerticalStack{
                                             .spacing = 12.0f,
                                             .content =
                                                 {
                                                     ui::SelectorBar{
                                                         .items                = {"One", "Two", "Three"},
                                                         .on_selection_changed = [&s](std::size_t index)
                                                         { s.tab = index; },
                                                     },
                                                     ui::HorizontalStack{
                                                         .spacing = 24.0f,
                                                         .content =
                                                             {
                                                                 ui::VerticalStack{
                                                                     .spacing = 4.0f,
                                                                     .content =
                                                                         {
                                                                             ui::Text{.text = "Match", .role = ui::TextRole::Caption},
                                                                             std::move(rebuilt),
                                                                         },
                                                                 },
                                                                 ui::VerticalStack{
                                                                     .spacing = 4.0f,
                                                                     .content =
                                                                         {
                                                                             ui::Text{.text = "Match with keep",
                                                                                      .role = ui::TextRole::Caption},
                                                                             std::move(kept),
                                                                         },
                                                                 },
                                                             },
                                                     },
                                                 },
                                         }},
                    }),
                    example({
                        .title    = "State gives each component its own state",
                        .code     = R"cpp(
                        // A counter that owns its count: three of them share nothing.
                        ui::Spec counter(std::string label)
                        {
                            return ui::State<ui::Property<int>>{
                                .content = [label = std::move(label)](ui::Property<int>& count) mutable -> ui::Spec
                                {
                                    return ui::HorizontalStack{
                                        .spacing         = 8,
                                        .child_alignment = ui::Alignment::Center,
                                        .content         = {
                                            ui::Button{.text = std::move(label), .on_click = [&count] { ++count; }},
                                            ui::Text{.text = ui::bind(count, [](int n) { return std::format("Clicked {} times", n); })},
                                        },
                                    };
                                },
                            };
                        }

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {counter("First"), counter("Second"), counter("Third")},
                        }
                    )cpp",
                        .controls = {ui::VerticalStack{
                            .spacing = 8.0f,
                            .content = {counter("First"), counter("Second"), counter("Third")},
                        }},
                    }),
                });
        });
}

} // namespace

std::span<const Page> layout_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Border", "A container control that draws a boundary line, background, or both, around another object.", border_page},
        {"Canvas",
         "A layout panel that supports absolute positioning of child elements relative to the top left corner of the "
         "canvas.",
         canvas_page},
        {"Expander", "A container with a header that can be expanded to show a body with more content.", expander_page},
        {"GroupBox", "A header and a frame around a group of related controls.", group_box_page},
        {"Grid", "A layout panel that supports arranging child elements in rows and columns.", grid_page},
        {"LayoutPanel", "A panel that uses a Layout object to measure and arrange its children.", layout_panel_page},
        {"RelativePanel", "A panel that uses relationships between elements to define layout.", relative_panel_page},
        {"SidePane", "A container that has 2 content areas, with multiple display options for the pane.", split_view_page},
        {"Stack",
         "A layout panel that arranges child elements into a single line that can be oriented horizontally or "
         "vertically.",
         stack_page},
        {"TilePanel",
         "A layout panel that supports arranging child elements in rows and columns. Each child element can span "
         "multiple "
         "rows and columns.",
         tile_panel_page},
        {"WrapPanel",
         "A layout panel that arranges child elements in rows or columns and automatically wraps them onto new lines.",
         wrap_panel_page},
        {"ScaleToFit", "A container control that scales its content to a specified size.", viewbox_page},
        {"Splitter", "A draggable divider that resizes adjacent grid tracks.", splitter_page},
        {"ResizeGripper", "A corner grip that resizes along both axes.", resize_gripper_page},
        {"TwoPane", "Two panes that reflow between side-by-side and stacked.", two_pane_view_page},
        {"Match", "Structure that follows state: visible, Lazy, Match and State.", match_page},
    });
    return Pages;
}

} // namespace gallery

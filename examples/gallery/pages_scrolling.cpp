// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <array>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <cmath>


using ui::operator""_fr;

namespace gallery
{
namespace
{
ui::Color text_control_border(const ui::Theme& t) // a text field's border
{
    return t.colors().control_stroke_strong;
}

ui::Spec options_panel(float min_width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .min_width       = min_width,
        .content         = std::move(children),
    };
}

// A label and a control in a two-column row, as the Scroll samples' option grids lay them.
ui::Spec option_row(const char* label, ui::Spec control)
{
    return ui::Grid{
        .columns        = {ui::AutoSize, 1_fr},
        .rows           = {ui::AutoSize},
        .column_spacing = 12.0f,
        .content        = {ui::Cell{.row     = 0,
                                    .column  = 0,
                                    .content = ui::Text{.text = label, .vertical_alignment = ui::Alignment::Center}},
                           ui::Cell{.row = 0, .column = 1, .content = std::move(control)}},
    };
}

// The option grids' centered group headings ("ScrollMode", "ScrollBarVisibility").
ui::Spec option_heading(const char* text)
{
    return ui::Text{.text = text, .horizontal_alignment = ui::Alignment::Center};
}

// ---- AnnotatedScrollBar ----

// The sample's colors: five runs of one color each.
struct ColorRun
{
    const char* name;
    ui::Color   color;
    int         count;
};
constexpr std::array<ColorRun, 5> ColorRuns{{
    {"Azure", ui::Color::from_rgba(0xF0, 0xFF, 0xFF), 32},
    {"Crimson", ui::Color::from_rgba(0xDC, 0x14, 0x3C), 50},
    {"Cyan", ui::Color::from_rgba(0x00, 0xFF, 0xFF), 8},
    {"Fuchsia", ui::Color::from_rgba(0xFF, 0x00, 0xFF), 70},
    {"Gold", ui::Color::from_rgba(0xFF, 0xD7, 0x00), 90},
}};
constexpr int                     ColorCount = 32 + 50 + 8 + 70 + 90;

constexpr float ItemWidth  = 120.0f;
constexpr float ItemHeight = 90.0f;

// The sample's TileLayout of color tiles: as many 120px columns as fit, rows of 90. It
// reports column count changes, which is when the sample re-annotates the bar.
class ColorTiles final : public ui::Element
{
public:
    explicit ColorTiles(ui::Callback<void(int)> on_columns_changed) : on_columns_changed_(std::move(on_columns_changed))
    {
    }

protected:
    ui::Size measure_override(ui::Size available, const ui::MeasureContext&) const override
    {
        const int columns = std::isfinite(available.width) ? std::max(1, static_cast<int>(available.width / ItemWidth)) : 1;
        const int rows = (ColorCount + columns - 1) / columns;
        return {static_cast<float>(columns) * ItemWidth, static_cast<float>(rows) * ItemHeight};
    }

    void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext&) override
    {
        const int columns = std::max(1, static_cast<int>(final_rect.width / ItemWidth));
        if (columns != columns_)
        {
            columns_ = columns;
            if (on_columns_changed_)
            {
                on_columns_changed_(columns_);
            }
        }
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext&) const override
    {
        int index = 0;
        for (const ColorRun& run : ColorRuns)
        {
            for (int i = 0; i < run.count; ++i, ++index)
            {
                const int   row = index / columns_;
                const float x   = bounds().x + static_cast<float>(index % columns_) * ItemWidth;
                const float y   = bounds().y + static_cast<float>(row) * ItemHeight;
                renderer.fill_rounded_rect(ui::Rect{x + 4.0f, y + 4.0f, ItemWidth - 8.0f, ItemHeight - 8.0f}, 4.0f, run.color);
            }
        }
    }


private:
    ui::Callback<void(int)> on_columns_changed_;
    int                     columns_ = 1;
};

struct AnnotatedScrollBarState
{
    ui::Control<ui::Scroll>             view;
    ui::Control<ui::AnnotatedScrollBar> bar;
    int                                 columns = 1;
};

// The offset of the row an item sits in.
double offset_of_item(int item, int columns)
{
    const int row = item / std::max(1, columns);
    return ItemHeight * static_cast<double>(row);
}

// The color of an item.
const char* item_color(int item)
{
    for (const ColorRun& run : ColorRuns)
    {
        if (item < run.count)
        {
            return run.name;
        }
        item -= run.count;
    }
    return ColorRuns.back().name;
}

// The detail text: the color of the last item of the run the offset falls in.
std::string offset_label(double offset, int columns)
{
    int end = 0;
    for (std::size_t i = 0; i + 1 < ColorRuns.size(); ++i)
    {
        end += ColorRuns[i].count;
        if (offset <= offset_of_item(end - 1, columns))
        {
            return item_color(end - 1);
        }
    }
    return item_color(end);
}

// One annotation per run, at the offset of the run's first item's row.
void populate_annotations(AnnotatedScrollBarState& s)
{
    if (s.bar == nullptr)
    {
        return;
    }
    std::vector<ui::AnnotatedScrollBarAnnotation> annotations;
    int                                           first = 0;
    for (const ColorRun& run : ColorRuns)
    {
        annotations.push_back({.text = run.name, .scroll_offset = offset_of_item(first, s.columns)});
        first += run.count;
    }
    s.bar->set_annotations(std::move(annotations));
}

ui::Spec annotated_scroll_bar_page(Context&)
{
    return with_state<AnnotatedScrollBarState>(
        [](AnnotatedScrollBarState& s) -> ui::Spec
        {
            auto tiles = std::make_unique<ColorTiles>(
                [&s](int columns)
                {
                    s.columns = columns;
                    populate_annotations(s);
                });
            tiles->set_margin(ui::Thickness{2.0f});

            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title   = "AnnotatedScrollBar linked to a Scroll.",
                    .code    = R"cpp(
                        using ui::operator""_fr;

                        ui::Control<ui::Scroll>         view;
                        ui::Control<ui::AnnotatedScrollBar> bar;

                        ui::Grid{
                            .columns = {1_fr, ui::AutoSize},
                            .rows    = {ui::AutoSize},
                            .content   = {
                                ui::Cell{.row     = 0,
                                 .column  = 0,
                                 .content = ui::Scroll{
                                     // The annotated bar replaces the built-in one.
                                     .vertical_scroll_bar_visibility = ui::ScrollBarVisibility::Hidden,
                                     .max_width                      = 800,
                                     .max_height                     = 500,
                                     .control                        = view,
                                     .content                        = std::move(tiles), // the color tiles
                                 }},
                                ui::Cell{.row     = 0,
                                 .column  = 1,
                                 .content = ui::AnnotatedScrollBar{
                                     .max_height = 500,
                                     // The tooltip text for the offset under the pointer;
                                     // color_at() is the app's own lookup, returning a std::string.
                                     .on_detail_text_requested = [&](double offset) { return color_at(offset); },
                                     .control                   = bar,
                                 }},
                            },
                        }

                        // Once built: link the bar to the view, and annotate where each color starts.
                        bar->set_target(view);
                        bar->set_annotations({{"Azure", 0.0}, {"Crimson", 450.0}, {"Cyan", 1170.0}});

                        bar->set_max_height(300); // annotations that would collide are hidden
                    )cpp",
                    .options = ui::VerticalStack{
                        .spacing     = 10.0f,
                        .child_alignment = ui::Alignment::Stretch,
                        .min_width   = 200.0f,
                        .content =
                            {
                                ui::Text{.text    = "Changing the AnnotatedScrollBar height lays its annotations out again.",
                                         .vertical_alignment = ui::Alignment::Center},
                                ui::Slider{.header = "AnnotatedScrollBar maximum height:",
                                           .value  = 500.0f,
                                           .min    = 100.0f,
                                           .max    = 500.0f,
                                           // Hides annotations that would collide once the room shrinks.
                                           .on_value_changed = [&s](double value)
                                           {
                                               if (s.bar != nullptr)
                                               {
                                                   s.bar->set_max_height(static_cast<float>(value));
                                               }
                                           }},
                            },
                    },
                    .controls =
                        {
                            ui::Grid{
                                .columns = {1_fr, ui::AutoSize},
                                .rows    = {ui::AutoSize},
                                .content =
                                    {
                                        ui::Cell{.row     = 0,
                                         .column  = 0,
                                         .content = ui::Scroll{.vertical_scroll_bar_visibility = ui::ScrollBarVisibility::Hidden,
                                                               .background = ui::Color::from_rgba(0xD3, 0xD3, 0xD3), // LightGray
                                                               .max_width  = 800.0f,
                                                               .max_height = 500.0f,
                                                               .control        = s.view,
                                                               .content    = std::move(tiles)}},
                                        ui::Cell{.row     = 0,
                                         .column  = 1,
                                         .content = ui::AnnotatedScrollBar{
                                             .margin     = ui::Edges{.left = 4.0f, .right = 48.0f},
                                             .max_height = 500.0f,
                                             .horizontal_alignment    = ui::Alignment::Trailing,
                                             // The color of the last item in the row under the pointer.
                                             .on_detail_text_requested = [&s](double offset) { return offset_label(offset, s.columns); },
                                             .control = s.bar,
                                         }},
                                    },
                            },
                        },
                }),
            }));

            // The sample's Loaded handler: the Scroll's vertical scroll controller becomes the bar.
            s.bar->set_target(s.view);
            populate_annotations(s);
            return page;
        });
}

// ---- Scroll ----

struct ScrollState
{
    // Example 1's options.
    ui::Control<ui::Scroll>               zoomable;
    ui::Property<ui::ScrollZoomMode>      zoom_mode{ui::ScrollZoomMode::Enabled};
    ui::Property<ui::ScrollMode>          horizontal_mode{ui::ScrollMode::Auto};
    ui::Property<ui::ScrollMode>          vertical_mode{ui::ScrollMode::Auto};
    ui::Property<ui::ScrollBarVisibility> horizontal_bar{ui::ScrollBarVisibility::Auto};
    ui::Property<ui::ScrollBarVisibility> vertical_bar{ui::ScrollBarVisibility::Auto};
    ui::Property<bool>                    pan{true};

    // Example 2's.
    ui::Control<ui::Scroll>    coasting;
    ui::Control<ui::NumberBox> velocity;
    double                     last_velocity = 30.0;

    // Example 3's.
    ui::Control<ui::Scroll> animated;
    std::size_t             animation = 0; // Default, Accordion, Teleportation
    double                  duration  = 1500.0;

    // Example 4's.
    ui::Property<float> position{0.0f};
};

// A column of sample photos, as the second and third samples scroll through.
ui::Spec photo_column(std::size_t first, std::size_t count, std::vector<const char*> names)
{
    ui::SpecList photos;
    for (std::size_t i = 0; i < count; ++i)
    {
        photos.push_back(ui::Image{.source        = sample_photo(first + i),
                                   .stretch       = ui::Stretch::Uniform,
                                   .accessibility = {.name = names[i]}});
    }
    return ui::VerticalStack{.child_alignment = ui::Alignment::Stretch, .content = std::move(photos)};
}

// NbVerticalVelocity_ValueChanged: stop any coasting, and keep the speed outside +-30, the
// minimum that moves anything, turning back at whichever end the view is resting against.
void change_velocity(ScrollState& s, double value)
{
    const double old        = s.last_velocity;
    s.last_velocity         = value;
    ui::ScrollControl* view = s.coasting;
    if (view == nullptr)
    {
        return;
    }
    view->scroll_by(0.0f, 0.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});

    double     velocity  = value;
    const bool at_top    = view->vertical_offset() == 0.0f;
    const bool at_bottom = view->vertical_offset() == view->scrollable_height();
    if (value <= 30.0 && value >= -30.0)
    {
        velocity = value < old ? (at_top ? 30.0 : -30.0) : (at_bottom ? -30.0 : 30.0);
    }
    else if (value < 30.0 && at_top)
    {
        velocity = 30.0;
    }
    else if (value > 30.0 && at_bottom)
    {
        velocity = -30.0;
    }
    if (s.velocity != nullptr)
    {
        s.velocity->set_value(velocity);
    }
    // An empty inertia decay rate: a constant velocity.
    view->add_scroll_velocity(ui::Point{0.0f, static_cast<float>(velocity)}, ui::Point{0.0f, 0.0f});
}

// The target vertical offset: a fifth of the way from whichever end the view is further from.
float target_vertical_offset(const ui::ScrollControl& view)
{
    const float range = view.scrollable_height();
    return view.vertical_offset() > range / 2.0f ? range / 5.0f : 4.0f * range / 5.0f;
}

// Scroll_ScrollAnimationStarting: the stock animation with the chosen duration, or one of the
// sample's two custom key-frame paths.
void customize_animation(const ScrollState& s, ui::ScrollAnimationStarting& args)
{
    args.animation.duration = ui::Duration{s.duration};
    if (s.animation == 0)
    {
        return;
    }
    const float                     h      = args.start_position.x;
    const float                     target = args.end_position.y;
    const float                     from   = args.start_position.y;
    std::vector<ui::ScrollKeyFrame> frames;
    if (s.animation == 1)
    {
        // Accordion: overshoot by a tenth of the distance, then settle in three halving swings.
        float delta = 0.1f * (target - from);
        for (int step = 0; step < 3; ++step)
        {
            frames.push_back({1.0f - 0.4f / std::pow(2.0f, static_cast<float>(step)), {h, target + delta}, {}});
            delta /= -2.0f;
        }
        frames.push_back({1.0f, {h, target}, {}});
    }
    else
    {
        // Teleportation: ease away slowly, jump most of the way, then ease in.
        const float delta = target - from;
        frames.push_back({0.499999f, {h, target - 0.9f * delta}, {ui::CubicBezier{1.0f, 0.0f, 1.0f, 0.0f}, 0}});
        frames.push_back({0.5f, {h, target - 0.1f * delta}, {ui::easing::Linear, 1}});
        frames.push_back({1.0f, {h, target}, {ui::CubicBezier{0.0f, 1.0f, 0.0f, 1.0f}, 0}});
    }
    args.animation.key_frames = std::move(frames);
}

// The fourth card, named so the page's list of cards stays shallow for the compiler.
ui::Spec bound_position_example(ScrollState& s)
{
    ui::Spec view = ui::Scroll{.vertical_offset      = s.position,
                               .width                = 400.0f,
                               .height               = 300.0f,
                               .horizontal_alignment = ui::Alignment::Leading,
                               .vertical_alignment   = ui::Alignment::Leading,
                               .content              = photo_column(3, 4, {"pond", "marina", "beach", "rampart"})};
    return example({
        .title = "A scroll position bound to a property.",
        .code  = R"cpp(
            ui::Property<float> position{0.0f};

            ui::Scroll{
                .vertical_offset = position, // the user's scrolling writes it, a write jumps
                .width           = 400,
                .height          = 300,
                .content         = ui::VerticalStack{/* ... a column of ui::Image ... */},
            }

            ui::Text{.text = ui::bind(position, [](float y) { return std::format("{:.0f} DIPs down", y); })}
            ui::Button{.text = "Top", .on_click = [&] { position = 0.0f; }}
            // Past the end: the view clamps the value and writes the clamped one back.
            ui::Button{.text = "Bottom", .on_click = [&] { position = 100000.0f; }}
        )cpp",
        .options = options_panel(200.0f,
                                 {
                                     ui::Text{.text = ui::bind(s.position,
                                                               [](float y) { return std::format("{:.0f} DIPs down", y); }),
                                              .accessibility = {.name = "scroll position"}},
                                     ui::Button{.text = "Top", .on_click = [&s] { s.position = 0.0f; }},
                                     ui::Button{.text = "Bottom", .on_click = [&s] { s.position = 100000.0f; }},
                                 }),
        .controls = {ui::VerticalStack{
            .spacing         = 16.0f,
            .child_alignment = ui::Alignment::Stretch,
            .content =
                {
                    ui::Text{.text = "Scroll the photos and the position on the right follows. The buttons write the "
                                     "position and the view jumps there.",
                             .wrapping = ui::TextWrapping::Wrap},
                    std::move(view),
                },
        }},
    });
}

ui::Spec scroll_mode_picker(const char* name, ui::Property<ui::ScrollMode>& target)
{
    return ui::ComboBox{
        .items = {"Enabled", "Disabled", "Auto"},
        .selected_index = ui::bind_choice(target, {ui::ScrollMode::Enabled, ui::ScrollMode::Disabled, ui::ScrollMode::Auto}),
        .accessibility = {.name = name},
    };
}

ui::Spec scroll_bar_picker(const char* name, ui::Property<ui::ScrollBarVisibility>& target)
{
    return ui::ComboBox{
        .items          = {"Auto", "Visible", "Hidden"},
        .selected_index = ui::bind_choice(target,
                                          {ui::ScrollBarVisibility::Auto,
                                           ui::ScrollBarVisibility::Visible,
                                           ui::ScrollBarVisibility::Hidden}),
        .accessibility  = {.name = name},
    };
}

ui::Spec scroll_view_page(Context&)
{
    return with_state<ScrollState>(
        [](ScrollState& s) -> ui::Spec
        {
            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title   = "Content inside of a Scroll.",
                    .code    = R"cpp(
                        ui::Control<ui::Scroll>                    view;
                        ui::Property<ui::ScrollZoomMode>            zoom_mode{ui::ScrollZoomMode::Enabled};
                        ui::Property<ui::ScrollMode>          horizontal_mode{ui::ScrollMode::Auto};
                        ui::Property<ui::ScrollMode>          vertical_mode{ui::ScrollMode::Auto};
                        ui::Property<ui::ScrollBarVisibility> horizontal_bar{ui::ScrollBarVisibility::Auto};
                        ui::Property<ui::ScrollBarVisibility> vertical_bar{ui::ScrollBarVisibility::Auto};
                        ui::Bitmap                                     photo; // loaded elsewhere

                        ui::Scroll{
                            // None: the content is measured at the view's size on both axes.
                            .content_orientation              = ui::ScrollContentOrientation::None,
                            .horizontal_scroll_mode           = horizontal_mode,
                            .vertical_scroll_mode             = vertical_mode,
                            .zoom_mode                        = zoom_mode,
                            .horizontal_scroll_bar_visibility = horizontal_bar,
                            .vertical_scroll_bar_visibility   = vertical_bar,
                            .pan_on_drag                      = true, // drag the photo to move it
                            .width                            = 400,
                            .height                           = 266,
                            .control                          = view,
                            .content                          = ui::Image{.source = photo, .stretch = ui::Stretch::Uniform},
                        }

                        view->zoom_to(4.0f); // what the Zoom factor box calls
                    )cpp",
                    .options = options_panel(
                        200.0f,
                        {
                            option_row("Zoom mode",
                                       ui::ComboBox{.items          = {"Enabled", "Disabled"},
                                                    .selected_index = ui::bind_choice(
                                                        s.zoom_mode, {ui::ScrollZoomMode::Enabled, ui::ScrollZoomMode::Disabled}),
                                                    .accessibility = {.name = "zoom mode"}}),
                            option_row("Zoom factor",
                                       ui::NumberBox{.value                 = 4.0,
                                                     .min                   = 0.1,
                                                     .max                   = 10.0,
                                                     .step                  = 1.0,
                                                     .large_step            = 10.0,
                                                     .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                                     // The sample's formatter: "04.0".
                                                     .number_formatter =
                                                         ui::DecimalFormatter{.integer_digits  = 2,
                                                                          .fraction_digits = 1,
                                                                          .rounder         = ui::IncrementNumberRounder{
                                                                              0.1, ui::RoundingAlgorithm::RoundHalfUp}},
                                                     .accessibility         = {.name = "zoom factor"},
                                                     .on_value_changed      = [&s](std::optional<double> value)
                                                     {
                                                         if (s.zoomable != nullptr && value)
                                                         {
                                                             s.zoomable->zoom_to(static_cast<float>(*value));
                                                         }
                                                     }}),
                            ui::ToggleSwitch{.header = "Pan on drag", .checked = ui::bind(s.pan)},
                            option_heading("ScrollMode"),
                            option_row("Horizontal", scroll_mode_picker("horizontal scroll mode", s.horizontal_mode)),
                            option_row("Vertical", scroll_mode_picker("vertical scroll mode", s.vertical_mode)),
                            option_heading("ScrollBarVisibility"),
                            option_row("Horizontal", scroll_bar_picker("horizontal scroll bar visibility", s.horizontal_bar)),
                            option_row("Vertical", scroll_bar_picker("vertical scroll bar visibility", s.vertical_bar)),
                        }),
                    .controls = {ui::VerticalStack{
                        .spacing     = 16.0f,
                        .child_alignment = ui::Alignment::Stretch,
                        .content =
                            {
                                ui::Text{.text = "This Scroll allows horizontal and vertical scrolling, as well as zooming. "
                                                 "Change the settings on the right to alter those capabilities or the built-in "
                                                 "scrollbars' visibility. With Pan on drag on, drag the zoomed photo to move it.",
                                         .wrapping = ui::TextWrapping::Wrap},
                                ui::Scroll{.content_orientation              = ui::ScrollContentOrientation::None,
                                           .horizontal_scroll_mode           = s.horizontal_mode,
                                           .vertical_scroll_mode             = s.vertical_mode,
                                           .zoom_mode                        = s.zoom_mode,
                                           .horizontal_scroll_bar_visibility = s.horizontal_bar,
                                           .vertical_scroll_bar_visibility   = s.vertical_bar,
                                           .focusable                         = true,
                                           .pan_on_drag                       = ui::bind(s.pan),
                                           .width                            = 400.0f,
                                           .height                           = 266.0f,
                                           .horizontal_alignment                          = ui::Alignment::Leading,
                                           .vertical_alignment                          = ui::Alignment::Leading,
                                           .control                              = s.zoomable,
                                           .content                          = ui::Image{.source  = sample_photo(8),
                                                                                         .stretch = ui::Stretch::Uniform,
                                                                                         .horizontal_alignment = ui::Alignment::Center,
                                                                                         .vertical_alignment = ui::Alignment::Center,
                                                                                         .accessibility = {.name = "cliff"}}},
                            },
                    }},
                }),
                example({
                    .title   = "Constant velocity scrolling.",
                    .code    = R"cpp(
                        ui::Control<ui::Scroll> view;

                        ui::Scroll{
                            .width   = 400,
                            .height  = 300,
                            .control = view,
                            .content = ui::VerticalStack{/* ... a column of ui::Image ... */},
                        }

                        ui::NumberBox{
                            .value            = 30.0,
                            .min              = -200.0,
                            .max              = 200.0,
                            .step             = 10.0,
                            .on_value_changed = [&](std::optional<double> velocity)
                            {
                                // Stop the scroll in progress, then start the new one.
                                view->scroll_by(0.0f, 0.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});
                                // A zero inertia decay rate keeps the velocity constant.
                                view->add_scroll_velocity(ui::Point{0.0f, static_cast<float>(velocity.value_or(0.0))},
                                                          ui::Point{0.0f, 0.0f});
                            },
                        }
                    )cpp",
                    .options = options_panel(200.0f,
                                             {
                                                 option_row("Vertical velocity",
                                                            ui::NumberBox{.value                 = 30.0,
                                                                          .min                   = -200.0,
                                                                          .max                   = 200.0,
                                                                          .step                  = 10.0,
                                                                          .large_step            = 30.0,
                                                                          .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                                                          .accessibility         = {.name = "vertical velocity"},
                                                                          .on_value_changed = [&s](std::optional<double> value) { change_velocity(s, value.value_or(0.0)); },
                                                                          .control              = s.velocity}),
                                             }),
                    .controls = {ui::VerticalStack{
                        .spacing     = 16.0f,
                        .child_alignment = ui::Alignment::Stretch,
                        .content =
                            {
                                ui::Text{.text     = "Set the vertical velocity to a value greater than 30 to scroll down, or a "
                                                     "value smaller than -30 to scroll up at a constant speed.",
                                         .wrapping = ui::TextWrapping::Wrap},
                                ui::Scroll{.focusable = true,
                                           .width       = 400.0f,
                                           .height      = 300.0f,
                                           .horizontal_alignment     = ui::Alignment::Leading,
                                           .vertical_alignment     = ui::Alignment::Leading,
                                           .control         = s.coasting,
                                           .content     = photo_column(9, 6, {"grapes", "rainier", "sunset", "treetops", "valley", "cliff"})},
                            },
                    }},
                }),
                example({
                    .title   = "Programmatic scroll with custom animation.",
                    .code    = R"cpp(
                        ui::Control<ui::Scroll> view;
                        double                      duration = 1500.0;

                        ui::Scroll{
                            .width  = 400,
                            .height = 300,
                            // Runs before each animated scroll; change the animation here.
                            .on_scroll_animation_starting = [&](ui::ScrollAnimationStarting& args)
                            {
                                const float x      = args.start_position.x;
                                const float from   = args.start_position.y;
                                const float target = args.end_position.y;
                                const float delta  = target - from;

                                args.animation.duration   = ui::Duration{duration};
                                // "Teleportation": ease away slowly, jump most of the way, ease in.
                                args.animation.key_frames = {
                                    {0.499999f, {x, target - 0.9f * delta}, {ui::CubicBezier{1.0f, 0.0f, 1.0f, 0.0f}, 0}},
                                    {0.5f, {x, target - 0.1f * delta}, {ui::easing::Linear, 1}},
                                    {1.0f, {x, target}, {ui::CubicBezier{0.0f, 1.0f, 0.0f, 1.0f}, 0}},
                                };
                            },
                            .control = view,
                            .content = ui::VerticalStack{/* ... a column of ui::Image ... */},
                        }

                        ui::Button{
                            .text     = "Scroll with animation",
                            .on_click = [&]
                            {
                                view->scroll_to(view->horizontal_offset(),
                                                0.8f * view->scrollable_height(),
                                                {.animation_mode = ui::ScrollAnimationMode::Enabled});
                            },
                        }
                    )cpp",
                    .options = options_panel(
                        320.0f,
                        {
                            option_row("Scroll with animation",
                                       ui::ComboBox{.items                = {"Default", "Accordion", "Teleportation"},
                                                    .selected_index       = 0,
                                                    .accessibility        = {.name = "vertical animation options"},
                                                    .on_selection_changed = [&s](std::optional<std::size_t> index) { s.animation = index.value_or(0); }}),
                            option_row("Animation duration (msec)",
                                       ui::NumberBox{.value                 = 1500.0,
                                                     .min                   = 1000.0,
                                                     .max                   = 5000.0,
                                                     .step                  = 500.0,
                                                     .large_step            = 1000.0,
                                                     .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                                     .accessibility         = {.name = "animation duration"},
                                                     .on_value_changed      = [&s](std::optional<double> value) { s.duration = value.value_or(0.0); }}),
                            ui::Button{.text          = "Scroll with animation",
                                       .accessibility = {.name = "scroll with animation"},
                                       .on_click      = [&s]
                                       {
                                           if (s.animated != nullptr)
                                           {
                                               s.animated->scroll_to(s.animated->horizontal_offset(),
                                                                     target_vertical_offset(*s.animated),
                                                                     {.animation_mode = ui::ScrollAnimationMode::Enabled});
                                           }
                                       }},
                        }),
                    .controls = {ui::VerticalStack{
                        .spacing     = 16.0f,
                        .child_alignment = ui::Alignment::Stretch,
                        .content =
                            {
                                ui::Text{.text     = "Pick an animation type and its duration and then click the button on the "
                                                     "right to launch a programmatic scroll.",
                                         .wrapping = ui::TextWrapping::Wrap},
                                ui::Scroll{.focusable = true,
                                           .width       = 400.0f,
                                           .height      = 300.0f,
                                           .horizontal_alignment     = ui::Alignment::Leading,
                                           .vertical_alignment     = ui::Alignment::Leading,
                                           .on_scroll_animation_starting = [&s](ui::ScrollAnimationStarting& args)
                                           { customize_animation(s, args); },
                                           .control     = s.animated,
                                           .content = photo_column(0,
                                                                   8,
                                                                   {"leaves",
                                                                    "carousel",
                                                                    "bicycles",
                                                                    "pond",
                                                                    "marina",
                                                                    "beach",
                                                                    "rampart",
                                                                    "mountain"})},
                            },
                    }},
                }),
                bound_position_example(s),
            }));

            // The sample's Loaded handler zooms the first view to 4, animated; the request waits
            // for the first layout.
            s.zoomable->zoom_to(4.0f);
            return page;
        });
}

// ---- PullToRefresh -------------------------------------------------------------------------------

// Stand-in for the sample's sun images: a sun in the page's text color, black on light and white on
// dark as the sample picks the image.
class Sun final : public ui::Element
{
public:
    Sun()
    {
        set_width(35.0f);
        set_height(35.0f);
    }

protected:
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{35.0f, 35.0f};
    }
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        const ui::Point  c     = bounds().center();
        const ui::Color  color = theme.colors().text;
        renderer.fill_ellipse(c, 8.0f, 8.0f, color);
        for (int i = 0; i < 8; ++i)
        {
            const float a = static_cast<float>(i) * 3.14159265f / 4.0f;
            renderer.stroke_line(ui::Point{c.x + std::cos(a) * 11.0f, c.y + std::sin(a) * 11.0f},
                                 ui::Point{c.x + std::cos(a) * 16.0f, c.y + std::sin(a) * 16.0f},
                                 color,
                                 2.0f);
        }
    }
};

struct RefreshPageState
{
    std::vector<std::string>               items1{"AcrylicBrush",
                                                  "ColorPicker",
                                                  "NavigationShell",
                                                  "Parallax",
                                                  "PersonPicture",
                                                  "PullToRefresh",
                                                  "Rating",
                                                  "Tabs",
                                                  "TreeView"};
    std::vector<std::string>               items2{"Mike", "Ben", "Barbra", "Claire", "Justin", "Shawn", "Drew", "Lili"};
    int                                    added1 = 0;
    int                                    added2 = 0;
    ui::Control<ui::ListView<std::string>> list1;
    ui::Control<ui::ListView<std::string>> list2;
    ui::Control<ui::PullToRefresh>         refresh1;
    ui::Control<ui::PullToRefresh>         refresh2;
};

ui::Spec pull_to_refresh_page(Context&)
{
    return with_state<RefreshPageState>(
        [](RefreshPageState& s)
        {
            auto   timer1 = std::make_unique<Timer>();
            auto   timer2 = std::make_unique<Timer>();
            Timer* t1     = timer1.get();
            Timer* t2     = timer2.get();

            auto sun  = std::make_unique<Sun>();
            Sun* icon = sun.get();
            return examples({
                example({
                    .title    = "Basic PullToRefresh",
                    .code     = R"cpp(
                        std::vector<std::string>               items{"AcrylicBrush", "ColorPicker", "NavigationShell"};
                        ui::Control<ui::ListView<std::string>> list;
                        ui::Control<ui::PullToRefresh>         refresh;

                        ui::PullToRefresh{
                            .on_refresh_requested = [&]
                            {
                                // Start the work here. The indicator spins until complete_refresh().
                                load_more();
                            },
                            .control = refresh,
                            .content = ui::ListView<std::string>{
                                .items     = items,
                                .height    = 200,
                                .min_width = 200,
                                .control   = list,
                            },
                        }

                        // When the work is done:
                        list->insert_item(0, "NewControl");
                        refresh->complete_refresh();

                        // The edge the list is pulled from:
                        refresh->set_pull_direction(ui::RefreshPullDirection::BottomToTop);
                    )cpp",
                    .options  = options_panel(160.0f,
                                             {ui::RadioButtons{
                                                 .header         = "Pull direction",
                                                 .items          = {"TopToBottom", "BottomToTop", "LeftToRight", "RightToLeft"},
                                                 .selected_index = 0,
                                                 .on_selection_changed =
                                                     [&s](std::optional<std::size_t> i)
                                                 {
                                                     constexpr std::array Directions{ui::RefreshPullDirection::TopToBottom,
                                                                                     ui::RefreshPullDirection::BottomToTop,
                                                                                     ui::RefreshPullDirection::LeftToRight,
                                                                                     ui::RefreshPullDirection::RightToLeft};
                                                     if (s.refresh1 && i.value_or(0) < Directions.size())
                                                     {
                                                         s.refresh1->set_pull_direction(Directions[i.value_or(0)]);
                                                     }
                                                 },
                                             }}),
                    .controls = {ui::PullToRefresh{
                                     .horizontal_alignment = ui::Alignment::Center,
                                     .vertical_alignment   = ui::Alignment::Center,
                                     .on_refresh_requested =
                                         [&s, t1]
                                     {
                                         // Timer1: half a second's "work", then a new item and the deferral completed.
                                         t1->start(500.0,
                                                   [&s, t1]
                                                   {
                                                       t1->stop();
                                                       s.list1->insert_item(0, "NewControl " + std::to_string(s.added1++));
                                                       s.refresh1->complete_refresh();
                                                   });
                                     },
                                     .control = s.refresh1,
                                     .content = ui::ListView<std::string>{.items                 = s.items1,
                                                             .selected_index        = std::nullopt,
                                                             .border_brush = text_control_border,
                                                             .border_thickness      = ui::Thickness{1.0f},
                                                             .height                = 200.0f,
                                                             .min_width             = 200.0f,
                                                             .control               = s.list1},
                                 },
                                 std::move(timer1)},
                }),
                example({
                    .title    = "Custom Icon PullToRefresh",
                    .code     = R"cpp(
                        std::vector<std::string>               friends{"Mike", "Ben", "Barbra", "Claire"};
                        ui::Control<ui::ListView<std::string>> list;
                        ui::Control<ui::PullToRefresh>         refresh;

                        ui::PullToRefresh{
                            .on_refresh_requested = [&]
                            {
                                // ... start the work, and when it is done:
                                list->insert_item(0, "New Friend");
                                refresh->complete_refresh();
                            },
                            .on_refresh_state_changed = [](const ui::PullToRefreshStateChangedArgs& args)
                            {
                                // Idle, Interacting, Pending, Refreshing: a place args.new_state animate the icon.
                            },
                            .control = refresh,
                            .content = ui::ListView<std::string>{
                                .items   = friends,
                                .width   = 200,
                                .height  = 200,
                                .control = list,
                            },
                            // Shown in place of the Refresh symbol.
                            .visualizer_content = ui::Image{.source = ui::Bitmap::read("sun.png"), .width = 35, .height = 35},
                        }
                    )cpp",
                    .controls = {ui::PullToRefresh{
                                     .on_refresh_requested =
                                         [&s, t2]
                                     {
                                         t2->start(800.0,
                                                   [&s, t2]
                                                   {
                                                       t2->stop();
                                                       s.list2->insert_item(0, "New Friend " + std::to_string(s.added2++));
                                                       s.refresh2->complete_refresh();
                                                   });
                                     },
                                     // rv2_RefreshStateChanged: the sun does not turn.
                                     .on_refresh_state_changed = [icon]
                                     { icon->stop_animation(ui::AnimatedProperty::Rotation); },
                                     .control            = s.refresh2,
                                     .content            = ui::ListView<std::string>{.items                 = s.items2,
                                                                        .selected_index        = std::nullopt,
                                                                        .border_brush = text_control_border,
                                                                        .border_thickness      = ui::Thickness{1.0f},
                                                                        .width                 = 200.0f,
                                                                        .height                = 200.0f,
                                                                        .horizontal_alignment  = ui::Alignment::Center,
                                                                        .control               = s.list2},
                                     .visualizer_content = std::move(sun),
                                 },
                                 std::move(timer2)},
                }),
            });
        });
}

} // namespace

std::span<const Page> scrolling_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"AnnotatedScrollBar",
         "A control that extends a regular vertical scrollbar's functionality for an easy navigation through large "
         "collections.",
         annotated_scroll_bar_page},
        {"PullToRefresh",
         "Provides the ability to pull on a collection of items in a list/grid to refresh the contents of the "
         "collection.",
         pull_to_refresh_page},
        {"Scroll", "A container control that lets the user pan and zoom its content.", scroll_view_page},
    });
    return Pages;
}

} // namespace gallery

// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- what every sample window shares ----

// The gallery's app icon, drawn rather than shipped: a rounded blue tile
// holding four white panes. 32x32, so both the caption's and the taskbar's sizes scale from it.
const ui::Bitmap& gallery_icon()
{
    static const ui::Bitmap icon = []
    {
        constexpr int   Size   = 32;
        constexpr float Radius = 6.0f;
        const ui::Color tile   = ui::Color::from_argb(0xFF0F6CBD);
        ui::Bitmap      image{Size, Size};
        for (int y = 0; y < Size; ++y)
        {
            for (int x = 0; x < Size; ++x)
            {
                // Rounded corners: outside the corner circles is transparent.
                const float cx = std::clamp(static_cast<float>(x) + 0.5f, Radius, Size - Radius);
                const float cy = std::clamp(static_cast<float>(y) + 0.5f, Radius, Size - Radius);
                const float dx = static_cast<float>(x) + 0.5f - cx;
                const float dy = static_cast<float>(y) + 0.5f - cy;
                if (dx * dx + dy * dy > Radius * Radius)
                {
                    continue;
                }
                // Four 9x9 panes, 2 apart, centered.
                const bool pane = x >= 6 && x < 26 && y >= 6 && y < 26 && x != 15 && x != 16 && y != 15 && y != 16;
                image.set(x, y, pane ? ui::colors::White : tile);
            }
        }
        return image;
    }();
    return icon;
}

ui::WindowIcon gallery_window_icon()
{
    return ui::WindowIcon{.image = gallery_icon()};
}

// Opens a sample window: content built against the window itself, in the gallery's theme, on
// Mica, with the gallery's icon.
template <typename Build>
ui::Window* open_sample(Context& ctx, ui::WindowOptions options, const Build& build)
{
    options.theme      = ctx.theme_request;
    options.backdrop   = ui::Backdrop::Mica;
    auto        owned  = std::make_unique<ui::Window>(std::move(options));
    ui::Window* window = owned.get();
    window->set_icon(gallery_window_icon());
    window->set_content(build(*window));
    return &ui::open_window(std::move(owned)); // destroyed by the loop once it is closed
}

// A window a page keeps talking to after it opened it. Cleared by the window's on_closed(); a
// page destroyed first takes its handler back, so neither side is left holding the other.
struct TrackedWindow
{
    ui::Window*        window = nullptr;
    ui::Property<bool> open{false};

    void track(ui::Window* opened)
    {
        window = opened;
        open   = opened != nullptr;
        if (opened != nullptr)
        {
            opened->on_closed(
                [this]
                {
                    window = nullptr;
                    open   = false;
                });
        }
    }

    TrackedWindow()                                = default;
    TrackedWindow(const TrackedWindow&)            = delete;
    TrackedWindow& operator=(const TrackedWindow&) = delete;
    ~TrackedWindow()
    {
        if (window != nullptr)
        {
            window->on_closed({});
        }
    }
};

// The sample windows' Close button content: the Cancel symbol and "Close" in the critical
// color, read from the theme the button is drawn in.
class CriticalCloseLabel final : public ui::Element
{
public:

protected:
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext& context) const override
    {
        const ui::Size text = context.measurer.measure(Text, context.theme.text_style);
        text_width_         = text.width;
        return {IconSize + Gap + text.width, std::max(IconSize, text.height)};
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        const ui::Color  color = theme.colors().critical;
        ui::draw_icon(renderer,
                      ui::symbol_glyph(ui::Symbol::Cancel),
                      ui::Rect{bounds().x, bounds().y, IconSize, bounds().height},
                      color,
                      IconSize,
                      theme.icon_font_family);
        ui::TextStyle style = theme.text_style;
        style.horizontal    = ui::TextAlign::Leading;
        renderer.draw_text(Text, ui::Rect{bounds().x + IconSize + Gap, bounds().y, text_width_, bounds().height}, color, style);
    }


private:
    static constexpr const char* Text        = "Close";
    static constexpr float       IconSize    = 20.0f; // a Symbol's
    static constexpr float       Gap         = 4.0f;  // the icon's Margin="0,0,4,0"
    mutable float                text_width_ = 0.0f;
};

ui::Spec tool_tipped(const char* tip, ui::Spec control)
{
    return ui::ToolTip{.anchor = std::move(control), .content = ui::Text{.text = tip, .wrapping = ui::TextWrapping::Wrap}};
}

// A 200-wide button whose label wraps and is centered, as the samples' text contents are.
ui::Spec wide_button(const char* text, ui::Callback<void()> on_click)
{
    return ui::Button{.width    = 200.0f,
                      .on_click = std::move(on_click),
                      .content  = ui::Text{.text           = text,
                                           .wrapping       = ui::TextWrapping::WrapWholeWords,
                                           .text_alignment = ui::TextAlignment::Center}};
}

ui::Spec close_button(ui::Window& window)
{
    return ui::Button{.margin        = ui::Edges{.top = 16.0f},
                      .width         = 200.0f,
                      .accessibility = {.name = "Close"},
                      .on_click      = [&window] { window.close(); },
                      .content       = std::make_unique<CriticalCloseLabel>()};
}

// The samples' stack, centered both ways.
ui::Spec centered(float spacing, ui::SpecList children)
{
    return ui::VerticalStack{.spacing              = spacing,
                             .horizontal_alignment = ui::Alignment::Center,
                             .vertical_alignment   = ui::Alignment::Center,
                             .content              = std::move(children)};
}

// The one-paragraph windows the title bar cards open: wrapping 16px text with a margin of 20 in
// the middle.
ui::Spec sample_message(const char* text)
{
    return ui::Text{.text                 = text,
                    .role                 = ui::TextRole::Subtitle,
                    .wrapping             = ui::TextWrapping::Wrap,
                    .text_alignment       = ui::TextAlignment::Center,
                    .margin               = ui::Thickness{20.0f},
                    .horizontal_alignment = ui::Alignment::Center,
                    .vertical_alignment   = ui::Alignment::Center};
}

ui::Spec options_panel(float width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// With content extended into the title bar and no TitleBar, the window drags by nothing, so a
// band-high DragArea stands in for the caption band.
ui::Spec with_caption_band(ui::Window& window, ui::Spec content)
{
    ui::Control<ui::DragArea> band;
    ui::Spec   view   = ui::Grid{.columns = {1_fr},
                                 .rows    = {1_fr},
                                 .content = {
                                     ui::Cell{.row     = 0,
                                              .column  = 0,
                                              .content = ui::DragArea{.vertical_alignment = ui::Alignment::Leading,
                                                                      .control            = band}},
                                     ui::Cell{.row = 0, .column = 0, .content = std::move(content)},
                                 }};
    const auto follow = [band](const ui::CaptionInsets& insets) { band->set_height(insets.height); };
    follow(window.caption_insets().get());
    band->add_subscription(window.caption_insets().observe(follow));
    return view;
}

ui::Spec paragraph(const char* text)
{
    return ui::Text{.text = text, .wrapping = ui::TextWrapping::Wrap};
}

std::string number(double value)
{
    return std::format("{}", value);
}

// ---- Windowing ----

struct WindowingState
{
    ui::Property<double>      width{640.0};
    ui::Property<double>      height{480.0};
    ui::Property<double>      min_width{320.0};
    ui::Property<double>      min_height{240.0};
    ui::Property<double>      max_width{960.0};
    ui::Property<double>      max_height{720.0};
    ui::Property<bool>        error_open{false};
    ui::Property<std::string> error{};
};

ui::Spec dimension_box(const char* header, ui::Property<double>& value)
{
    return ui::NumberBox{.header                = header,
                         .value                 = value.get(),
                         .min                   = 1.0,
                         .max                   = 10000.0,
                         .step                  = 10.0,
                         .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
                         .accessibility         = {.name = header},
                         .on_value_changed      = [&value](std::optional<double> v) { value = v.value_or(0.0); }};
}

void open_configured_window(Context& ctx, WindowingState& s)
{
    const double values[] =
        {s.width.get(), s.height.get(), s.min_width.get(), s.min_height.get(), s.max_width.get(), s.max_height.get()};
    if (std::any_of(std::begin(values), std::end(values), [](double v) { return !std::isfinite(v); }))
    {
        s.error      = "Enter a value for every dimension.";
        s.error_open = true;
        return;
    }
    if (s.min_width.get() > s.width.get() || s.width.get() > s.max_width.get() || s.min_height.get() > s.height.get() ||
        s.height.get() > s.max_height.get())
    {
        s.error      = "Width and Height must be within their minimum and maximum limits.";
        s.error_open = true;
        return;
    }
    s.error_open = false;

    const auto  dip     = [](double v) { return static_cast<float>(v); };
    std::string message = std::format(
        "Initial client area: {} by {} DIPs. Width is constrained to {} to {} DIPs and height "
        "to {} to {} DIPs. Try resizing this window.",
        number(s.width.get()),
        number(s.height.get()),
        number(s.min_width.get()),
        number(s.max_width.get()),
        number(s.min_height.get()),
        number(s.max_height.get()));
    open_sample(ctx,
                {.title    = "Window client size and constraints (experimental)",
                 .size     = ui::Size{dip(s.width.get()), dip(s.height.get())},
                 .style    = {.maximize_button = false},
                 .min_size = {dip(s.min_width.get()), dip(s.min_height.get())},
                 .max_size = {dip(s.max_width.get()), dip(s.max_height.get())}},
                [message = std::move(message)](ui::Window&) -> ui::Spec
                { return ui::Text{.text = message, .wrapping = ui::TextWrapping::Wrap, .margin = ui::Thickness{24.0f}}; });
}

ui::Spec windowing_page(Context& ctx)
{
    return with_state<WindowingState>(
        [&ctx](WindowingState& s)
        {
            return ui::VerticalStack{
                .spacing     = 8.0f,
                .child_alignment = ui::Alignment::Stretch,
                .content =
                    {
                        paragraph("Create top-level windows and configure their client-area size. Each example opens "
                                  "a "
                                  "separate window."),
                        examples(
                            {
                                example({
                                    .title   = "Create a top-level Window",
                                    .code    = R"cpp(
                                        // The app owns the window; keep it alive for as long as it is open.
                                        ui::Window child{{
                                            .size        = ui::Size{500, 500},
                                            .style       = {.extend_content_into_title_bar = true},
                                        }};
                                        child.set_content(ui::Text{
                                            .text                 = "New child window!",
                                            .horizontal_alignment = ui::Alignment::Center,
                                            .vertical_alignment   = ui::Alignment::Center,
                                        });
                                        child.show();
                                    )cpp",
                                    .stacked = true,
                                    .controls =
                                        {
                                            paragraph("This example opens the window with a 500 by 500 client area "
                                                      "(WindowOptions::size, in DIPs)."),
                                            ui::Button{.text          = "Create new Window",
                                                       .accessibility = {.name = "Create a top-level window"},
                                                       .on_click =
                                                           [&ctx]
                                                       {
                                                           open_sample(ctx,
                                                                       {.size = ui::Size{500.0f, 500.0f},
                                                                        .style = {.extend_content_into_title_bar = true}},
                                                                       [](ui::Window& window) -> ui::Spec { return with_caption_band(window, ui::Text{.text = "New child window!", .horizontal_alignment = ui::Alignment::Center, .vertical_alignment = ui::Alignment::Center}); });
                                                       }},
                                        },
                                }),
                                example(
                                    {
                                        .title   = "Configure the client size and constraints",
                                        .code    = R"cpp(
                                            // All sizes are client-area sizes in DIPs.
                                            ui::Window sized{{
                                                .title       = "Window client size and constraints",
                                                .size        = ui::Size{640, 480},
                                                .style       = {.maximize_button = false},
                                                .min_size    = {320, 240},
                                                .max_size    = {960, 720},
                                            }};
                                            sized.set_content(ui::Text{
                                                .text     = "Try resizing this window.",
                                                .wrapping = ui::TextWrapping::Wrap,
                                                .margin   = ui::Thickness{24},
                                            });
                                            sized.show();
                                        )cpp",
                                        .stacked = true,
                                        .options = ui::
                                            Grid{.columns        = {1_fr, 1_fr},
                                                 .rows           = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                                 .row_spacing    = 12.0f,
                                                 .column_spacing = 12.0f,
                                                 .width          = 260.0f,
                                                 .content =
                                                     {
                                                         ui::Cell{.row = 0, .column = 0, .content = dimension_box("Width", s.width)},
                                                         ui::Cell{.row = 0, .column = 1, .content = dimension_box("Height", s.height)},
                                                         ui::Cell{.row = 1, .column = 0, .content = dimension_box("Minimum width", s.min_width)},
                                                         ui::Cell{.row = 1, .column = 1, .content = dimension_box("Minimum height", s.min_height)},
                                                         ui::Cell{.row = 2, .column = 0, .content = dimension_box("Maximum width", s.max_width)},
                                                         ui::Cell{.row = 2, .column = 1, .content = dimension_box("Maximum height", s.max_height)},
                                                     }},
                                        .controls =
                                            {
                                                paragraph("Configure the initial, minimum, and maximum client-area "
                                                          "dimensions in "
                                                          "device-independent pixels (DIPs). The initial size must be "
                                                          "within the "
                                                          "minimum and maximum limits."),
                                                ui::InfoBar{.title     = "Check the window dimensions",
                                                            .message   = s.error,
                                                            .is_open   = s.error_open,
                                                            .severity  = ui::InfoBarSeverity::Error,
                                                            .on_closed = [&s](ui::PopupCloseReason)
                                                            { s.error_open = false; }},
                                                ui::Button{.text          = "Open configured window",
                                                           .accessibility = {.name = "Open a window with the "
                                                                                     "configured client size and "
                                                                                     "constraints"},
                                                           .on_click = [&ctx, &s] { open_configured_window(ctx, s); }},
                                            },
                                    }),
                            }),
                    },
            };
        });
}

// ---- Window options ----

struct WindowPageState
{
    // Creating and customizing
    ui::Property<std::string> title{"This is a title"};
    ui::Property<double>      width{800.0};
    ui::Property<double>      height{500.0};
    ui::Property<double>      x{50.0};
    ui::Property<double>      y{50.0};

    // Frame and caption buttons
    ui::Property<bool> always_on_top{false};
    ui::Property<bool> maximizable{true};
    ui::Property<bool> minimizable{true};
    ui::Property<bool> resizable{true};
    ui::Property<bool> has_border{true};
    ui::Property<bool> has_title_bar{true};
    ui::Subscription   border_rule;    // no border takes the title bar with it...
    ui::Subscription   title_bar_rule; // ...and a title bar needs a border

    // Minimum and maximum size
    ui::Property<double> min_width{400.0};
    ui::Property<double> min_height{400.0};
    ui::Property<double> max_width{1000.0};
    ui::Property<double> max_height{1000.0};

    // Picture-in-picture
    ui::Property<std::size_t> overlay_size{0};
};

// The frame card's note, with the names in SemiBold. It is
// the InfoBar's content rather than its message, so it can carry runs; the negative top margin
// pulls it up under the lone title, the right one keeps it off the bar's edge as a message is.
ui::Spec overlapped_warning()
{
    const auto bold = [](const char* text) { return ui::Inline{.text = text, .weight = ui::FontWeight::SemiBold}; };
    return ui::RichText{
        .blocks            = {{.inlines = {{.text = "A title bar needs a border. "},
                                           bold("Frame::Standard"),
                                           {.text = " has both, "},
                                           bold("Frame::Borderless"),
                                           {.text = " only the border and "},
                                           bold("Frame::None"),
                                           {.text = " neither. Turning "},
                                           bold("Border"),
                                           {.text = " off here turns "},
                                           bold("Title bar"),
                                           {.text = " off too, and turning "},
                                           bold("Title bar"),
                                           {.text = " on turns "},
                                           bold("Border"),
                                           {.text = " on."}}}},
        .selection_enabled = false,
        .margin            = ui::Edges{.top = -16.0f, .right = 16.0f, .bottom = 16.0f},
    };
}

ui::Spec bool_switch(const char* header, const char* tip, ui::Property<bool>& value)
{
    return tool_tipped(tip,
                       ui::ToggleSwitch{.header = header, .checked = value, .checked_text = "true", .unchecked_text = "false"});
}

ui::Spec number_box(const char* header, double min, double max, ui::Property<double>& value, bool spin = true)
{
    return ui::NumberBox{.header                = header,
                         .value                 = value.get(),
                         .min                   = min,
                         .max                   = max,
                         .step                  = 10.0,
                         .large_step            = 100.0,
                         .spin_button_placement = spin ? ui::NumberBoxSpinButtonPlacementMode::Inline
                                                       : ui::NumberBoxSpinButtonPlacementMode::Hidden,
                         .on_value_changed      = [&value](std::optional<double> v) { value = v.value_or(0.0); }};
}

// The frame and size samples' buttons: minimize, and minimize now and restore three seconds later.
ui::Spec minimize_buttons(ui::Window& window, ui::SpecList before)
{
    ui::SpecList buttons = std::move(before);
    buttons.push_back(ui::Button{.text     = "Minimize",
                                 .width    = 200.0f,
                                 .on_click = [&window] { window.request_state(ui::WindowState::Minimized); }});
    buttons.push_back(wide_button("Minimize and restore the window after 3 seconds",
                                  [&window]
                                  {
                                      window.request_state(ui::WindowState::Minimized);
                                      window.post_after(std::chrono::seconds{3}, [&window] { window.restore(); });
                                  }));
    buttons.push_back(close_button(window));
    return centered(10.0f, std::move(buttons));
}

// A NumberBox cleared to NaN counts as `fallback`: NaN is no size at all.
double or_default(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

void show_sample_window_1(Context& ctx, const WindowPageState& s)
{
    open_sample(ctx,
                {.title    = s.title.get(),
                 .size     = {static_cast<float>(or_default(s.width.get(), 800.0)),
                              static_cast<float>(or_default(s.height.get(), 500.0))},
                 .position = ui::Point{static_cast<float>(or_default(s.x.get(), 0.0)),
                                       static_cast<float>(or_default(s.y.get(), 0.0))}},
                [](ui::Window& window) -> ui::Spec
                {
                    return centered(8.0f,
                                    {
                                        tool_tipped("Hides the window from all UX representations in the system but "
                                                    "keeps the object "
                                                    "alive.",
                                                    ui::Button{.text     = "Hide",
                                                               .width    = 200.0f,
                                                               .on_click = [&window] { window.hide(); }}),
                                        tool_tipped("Hides and then shows the window and activates it after 3 seconds.",
                                                    wide_button("Hide and show the window after 3 seconds",
                                                                [&window]
                                                                {
                                                                    window.hide();
                                                                    window.post_after(std::chrono::seconds{3},
                                                                                      [&window] { window.show(); });
                                                                })),
                                        tool_tipped("Closes the window and releases all resources.", close_button(window)),
                                    });
                });
}

void show_centered_window(Context& ctx)
{
    ui::Window* window = open_sample(ctx,
                                     {},
                                     [](ui::Window&) -> ui::Spec
                                     {
                                         return centered(8.0f,
                                                         {ui::Text{.text           = "This is a centered sample window",
                                                                   .role           = ui::TextRole::Title,
                                                                   .text_alignment = ui::TextAlignment::Center}});
                                     });
    // Centered: the work area of the display it opened on, less its own size, halved.
    if (window != nullptr)
    {
        const std::optional<ui::Rect> area   = window->work_area();
        const std::optional<ui::Rect> bounds = window->bounds();
        if (area && bounds)
        {
            window->move({area->x + (area->width - bounds->width) * 0.5f, area->y + (area->height - bounds->height) * 0.5f});
        }
    }
}

// The Border and Title bar switches as a Frame: both is the ordinary frame; a border without a
// title bar is the borderless frame (DWM still draws its edge and shadow); neither is a bare popup.
ui::Frame presenter_frame(bool has_border, bool has_title_bar)
{
    if (!has_border)
    {
        return ui::Frame::None;
    }
    return has_title_bar ? ui::Frame::Standard : ui::Frame::Borderless;
}

// A sample window in the gallery's theme whose frame that theme draws.
void show_themed_window(Context& ctx)
{
    open_sample(ctx,
                {.title = "Themed frame", .style = {.frame = ui::Frame::Themed}},
                [](ui::Window&) -> ui::Spec
                {
                    return centered(8.0f,
                                    {ui::Text{.text           = "The theme draws this window's caption and border",
                                              .role           = ui::TextRole::Title,
                                              .text_alignment = ui::TextAlignment::Center}});
                });
}

void show_overlapped_window(Context& ctx, const WindowPageState& s)
{
    open_sample(ctx,
                {.style = {.resizable       = s.resizable.get(),
                           .minimize_button = s.minimizable.get(),
                           .maximize_button = s.maximizable.get(),
                           .always_on_top   = s.always_on_top.get(),
                           .frame           = presenter_frame(s.has_border.get(), s.has_title_bar.get())}},
                [](ui::Window& window) -> ui::Spec
                {
                    return minimize_buttons(window,
                                            {
                                                ui::Button{.text  = ui::bind(window.state(),
                                                                             [](ui::WindowState state)
                                                                             {
                                                                                return std::string{
                                                                                    state == ui::WindowState::Maximized
                                                                                        ? "Res"
                                                                                          "tor"
                                                                                          "e"
                                                                                        : "Max"
                                                                                          "imi"
                                                                                          "z"
                                                                                          "e"};
                                                                             }),
                                                           .width = 200.0f,
                                                           .on_click =
                                                               [&window]
                                                           {
                                                               if (window.state().get() == ui::WindowState::Maximized)
                                                               {
                                                                   window.restore();
                                                               }
                                                               else
                                                               {
                                                                   window.request_state(ui::WindowState::Maximized);
                                                               }
                                                           }},
                                            });
                });
}

void show_size_limited_window(Context& ctx, const WindowPageState& s)
{
    const auto dip = [](double v) { return static_cast<float>(or_default(v, 0.0)); };
    open_sample(ctx,
                {.size     = {800, 500},
                 .style    = {.maximize_button = false},
                 .min_size = {dip(s.min_width.get()), dip(s.min_height.get())},
                 .max_size = {dip(s.max_width.get()), dip(s.max_height.get())}},
                [](ui::Window& window) -> ui::Spec { return minimize_buttons(window, {}); });
}

void show_modal_window(Context& ctx)
{
    // A dialog window: no minimize or maximize button, a fixed size.
    open_sample(ctx,
                {.size  = {400, 300},
                 .style = {.resizable = false, .minimize_button = false, .maximize_button = false},
                 .owner = &ctx.window,
                 .modal = true},
                [](ui::Window& window) -> ui::Spec
                {
                    return centered(8.0f,
                                    {
                                        ui::Text{.text                 = "Modal Window",
                                                 .role                 = ui::TextRole::Title,
                                                 .text_alignment       = ui::TextAlignment::Center,
                                                 .horizontal_alignment = ui::Alignment::Center},
                                        ui::Text{.text           = "This is a modal window owned by the gallery "
                                                                   "window.",
                                                 .wrapping       = ui::TextWrapping::Wrap,
                                                 .text_alignment = ui::TextAlignment::Center},
                                        ui::HorizontalStack{.spacing              = 8.0f,
                                                            .horizontal_alignment = ui::Alignment::Center,
                                                            .content =
                                                                {
                                                                    ui::Button{.text    = "OK",
                                                                               .variant = ui::ButtonVariant::Accent,
                                                                               .width   = 80.0f,
                                                                               .on_click = [&window] { window.close(); }},
                                                                    ui::Button{.text  = "Cancel",
                                                                               .width = 80.0f,
                                                                               .on_click = [&window] { window.close(); }},
                                                                }},
                                    });
                });
}

void show_full_screen_window(Context& ctx)
{
    open_sample(ctx,
                {.initial_state = ui::WindowState::Fullscreen},
                [](ui::Window& window) -> ui::Spec
                {
                    return centered(8.0f,
                                    {
                                        ui::Text{.text           = "This window is running in Fullscreen mode",
                                                 .role           = ui::TextRole::Title,
                                                 .text_alignment = ui::TextAlignment::Center},
                                        ui::Button{.width                = 200.0f,
                                                   .horizontal_alignment = ui::Alignment::Center,
                                                   .accessibility        = {.name = "Close"},
                                                   .on_click             = [&window] { window.close(); },
                                                   .content              = std::make_unique<CriticalCloseLabel>()},
                                    });
                });
}

constexpr std::array<const char*, 3> OverlaySizes{"Small", "Medium", "Large"};
constexpr std::array<const char*, 3> OverlayShares{"5%", "15%", "25%"};

// Picture-in-picture as a set of window options: always on top,
// no minimize or maximize button, and a 16:9 window covering the given share of the work area.
void show_compact_overlay_window(Context& ctx, std::size_t size)
{
    constexpr std::array<float, 3> Share{0.05f, 0.15f, 0.25f};
    const ui::Rect                 area   = ctx.window.work_area().value_or(ui::Rect{0.0f, 0.0f, 1920.0f, 1040.0f});
    const float                    width  = std::sqrt(Share[size] * area.width * area.height * 16.0f / 9.0f);
    const float                    height = width * 9.0f / 16.0f;
    open_sample(ctx,
                {.size  = ui::Size{width, height},
                 .style = {.minimize_button = false, .maximize_button = false, .always_on_top = true}},
                [](ui::Window&) -> ui::Spec
                {
                    return ui::Text{.text                 = "This window is in picture-in-picture mode.",
                                    .wrapping             = ui::TextWrapping::Wrap,
                                    .text_alignment       = ui::TextAlignment::Center,
                                    .horizontal_alignment = ui::Alignment::Center,
                                    .vertical_alignment   = ui::Alignment::Center};
                });
}

ui::Spec window_page(Context& ctx)
{
    return with_state<WindowPageState>(
        [&ctx](WindowPageState& s)
        {
            // A title bar needs a border: each switch keeps the other consistent.
            s.border_rule = s.has_border.observe(
                [&s](const bool& on)
                {
                    if (!on)
                    {
                        s.has_title_bar = false;
                    }
                });
            s.title_bar_rule = s.has_title_bar.observe(
                [&s](const bool& on)
                {
                    if (on)
                    {
                        s.has_border = true;
                    }
                });
            return ui::VerticalStack{
                .spacing     = 8.0f,
                .child_alignment = ui::Alignment::Stretch,
                .content =
                    {
                        ui::Text{.text = "General usage of Window", .role = ui::TextRole::Subtitle},
                        examples({
                            example({
                                .title = "Creating and customizing a Window",
                                .code  = R"cpp(
                                    // size is the client area, position the outer top-left corner, in DIPs.
                                    ui::Window sample{{
                                        .title    = "This is a title",
                                        .size    = {800, 500},
                                        .position = ui::Point{50, 50},
                                    }};
                                    sample.set_content(ui::VerticalStack{
                                        .spacing              = 8,
                                        .horizontal_alignment = ui::Alignment::Center,
                                        .vertical_alignment   = ui::Alignment::Center,
                                        .content = {
                                            // Hidden, the window is gone from the screen and the taskbar but stays alive.
                                            ui::Button{.text = "Hide", .on_click = [&sample] { sample.hide(); }},
                                            ui::Button{.text = "Close", .on_click = [&sample] { sample.close(); }},
                                        },
                                    });
                                    sample.show();
                                )cpp",
                                .options =
                                    options_panel(
                                        280.0f,
                                        {
                                            ui::Text{.text = "Window title", .role = ui::TextRole::BodyStrong},
                                            ui::TextBox{.placeholder_text = "Enter window title",
                                                        .text             = s.title.get(),
                                                        .on_text_changed  = [&s](const std::string& text) { s.title = text; }},
                                            ui::Text{.text = "Window size", .role = ui::TextRole::BodyStrong},
                                            ui::HorizontalStack{.spacing  = 8.0f,
                                                           .content = {number_box("Width", 200.0, 1000.0, s.width),
                                                                        number_box("Height", 200.0, 700.0, s.height)}},
                                            ui::Text{.text = "Window position", .role = ui::TextRole::BodyStrong},
                                            ui::HorizontalStack{.spacing  = 8.0f,
                                                           .content = {number_box("X", 0.0, 800.0, s.x),
                                                                        number_box("Y", 0.0, 300.0, s.y)}},
                                        }),
                                .controls =
                                    {
                                        ui::Button{.text = "Show window", .on_click = [&ctx, &s] { show_sample_window_1(ctx, s); }},
                                    },
                            }),
                            example({
                                .title = "Centering a Window in the display's work area",
                                .code  = R"cpp(
                                    ui::Window sample{{.title = "Centered"}};
                                    sample.set_content(ui::Text{.text = "This is a centered sample window"});
                                    sample.show();

                                    // The work area of the display it opened on, less its own size, halved.
                                    const std::optional<ui::Rect> area   = sample.work_area();
                                    const std::optional<ui::Rect> bounds = sample.bounds();
                                    if (area && bounds)
                                    {
                                        sample.move({area->x + (area->width - bounds->width) * 0.5f,
                                                     area->y + (area->height - bounds->height) * 0.5f});
                                    }
                                )cpp",
                                .controls =
                                    {
                                        ui::Button{.text = "Show centered sample window", .on_click = [&ctx] { show_centered_window(ctx); }},
                                    },
                            }),
                        }),
                        ui::Text{.text = "Window styles", .role = ui::TextRole::Subtitle, .margin = ui::Edges{.top = 16.0f}},
                        examples({
                            example({
                                .title   = "A window's frame and caption buttons",
                                .code    = R"cpp(
                                    ui::Window sample{{
                                        .style = {
                                            .resizable     = true,
                                            .minimize_button  = true,
                                            .maximize_button  = true,
                                            .always_on_top = false,
                                            // Standard: border and title bar. Borderless: border only. None: neither.
                                            .frame         = ui::Frame::Standard,
                                        },
                                    }};
                                    sample.set_content(ui::VerticalStack{
                                        .spacing = 10,
                                        .content = {
                                            ui::Button{
                                                .text     = "Maximize",
                                                .on_click = [&sample] { sample.request_state(ui::WindowState::Maximized); },
                                            },
                                            ui::Button{
                                                .text     = "Minimize",
                                                .on_click = [&sample] { sample.request_state(ui::WindowState::Minimized); },
                                            },
                                            ui::Button{.text = "Restore", .on_click = [&sample] { sample.restore(); }},
                                        },
                                    });
                                    sample.show();
                                )cpp",
                                .stacked = true,
                                .options = options_panel(
                                    180.0f,
                                    {
                                        bool_switch("Always on top",
                                                    "Keeps the window above all other windows when enabled.",
                                                    s.always_on_top),
                                        bool_switch("Maximize button",
                                                    "Controls the visibility and enable state of the maximize button.",
                                                    s.maximizable),
                                        bool_switch("Minimize button",
                                                    "Controls the visibility and enable state of the minimize button.",
                                                    s.minimizable),
                                        bool_switch("Resizable", "Allows resizing the window by dragging its edges.", s.resizable),
                                        bool_switch("Border", "Determines if the window has a visible outer border.", s.has_border),
                                        bool_switch("Title bar", "Specifies whether the window includes a system title bar.", s.has_title_bar),
                                    }),
                                .controls =
                                    {
                                        paragraph("A ui::WindowStyle sets whether a window can be resized, which caption "
                                                  "buttons it has, whether it stays on top and which frame it gets. The "
                                                  "default is a standard resizable window with all caption buttons."),
                                        ui::InfoBar{.title       = "Warning",
                                                    .is_open     = true,
                                                    .severity    = ui::InfoBarSeverity::Warning,
                                                    .closable    = false,
                                                    .content     = overlapped_warning()},
                                        ui::Button{.text     = "Show window",
                                                   .horizontal_alignment  = ui::Alignment::Leading,
                                                   .on_click = [&ctx, &s] { show_overlapped_window(ctx, s); }},
                                    },
                            }),
                            example({
                                .title   = "A window frame drawn by the theme",
                                .code    = R"cpp(
                                    ui::Window sample{{
                                        .title = "Themed frame",
                                        .theme = ui::ThemeRequest{.variant = ui::ThemeVariant::Windows9x},
                                        // The theme draws the caption and border; Fluent keeps the system frame.
                                        .style = {.frame = ui::Frame::Themed},
                                    }};
                                    sample.set_content(ui::Text{.text = "Windows 95 draws this caption and border."});
                                    sample.show();
                                )cpp",
                                .stacked = true,
                                .controls =
                                    {
                                        paragraph("With Frame::Themed the theme draws the window's caption and border. "
                                                  "The Windows 1.x, 2.x, 3.x, 9x, XP, Aero and MDL packs each draw "
                                                  "their own era's frame and caption buttons. Aero draws glass over the "
                                                  "blurred desktop while transparency effects are on, MDL the flat "
                                                  "Windows 8 frame in the window color. "
                                                  "Fluent 2, Fluent and MDL2 keep the system frame. This gallery "
                                                  "window uses Frame::Themed, so picking a theme in the header switches "
                                                  "its frame too."),
                                        ui::Button{.text                 = "Show window",
                                                   .horizontal_alignment = ui::Alignment::Leading,
                                                   .on_click             = [&ctx] { show_themed_window(ctx); }},
                                    },
                            }),
                            example({
                                .title   = "Setting a window's minimum and maximum size",
                                .code    = R"cpp(
                                    // Limits apply to the client area, in DIPs; zero leaves an axis unconstrained.
                                    // A window with a maximum size is better off without a maximize button.
                                    ui::Window sample{{
                                        .size    = {800, 500},
                                        .style    = {.maximize_button = false},
                                        .min_size = {400, 400},
                                        .max_size = {1000, 1000},
                                    }};
                                    sample.show();
                                )cpp",
                                .stacked = true,
                                .options = options_panel(180.0f,
                                                         {
                                                             number_box("Minimum width", 0.0, 10000.0, s.min_width, false),
                                                             number_box("Minimum height", 0.0, 10000.0, s.min_height, false),
                                                             number_box("Maximum width", 0.0, 10000.0, s.max_width, false),
                                                             number_box("Maximum height", 0.0, 10000.0, s.max_height, false),
                                                         }),
                                .controls =
                                    {
                                        paragraph("The minimum and maximum width and height can be set on a window. When "
                                                  "setting the maximum width or height, it's recommended to disable the window "
                                                  "maximization."),
                                        ui::Button{.text     = "Show window",
                                                   .horizontal_alignment  = ui::Alignment::Leading,
                                                   .on_click = [&ctx, &s] { show_size_limited_window(ctx, s); }},
                                    },
                            }),
                            example({
                                .title   = "A modal window",
                                .code    = R"cpp(
                                    // An owned, modal window: its owner takes no input until it is closed.
                                    ui::Window dialog{{
                                        .size  = {400, 300},
                                        .style  = {.resizable = false, .minimize_button = false, .maximize_button = false},
                                        .owner  = &window,
                                        .modal  = true,
                                    }};
                                    dialog.set_content(ui::VerticalStack{
                                        .spacing              = 8,
                                        .horizontal_alignment = ui::Alignment::Center,
                                        .vertical_alignment   = ui::Alignment::Center,
                                        .content = {
                                            ui::Text{.text = "Modal Window", .role = ui::TextRole::Title},
                                            ui::Button{
                                                .text     = "OK",
                                                .variant  = ui::ButtonVariant::Accent,
                                                .on_click = [&dialog] { dialog.close(); },
                                            },
                                        },
                                    });
                                    dialog.show();
                                )cpp",
                                .stacked = true,
                                .controls =
                                    {
                                        paragraph("A modal window is a separate window that blocks interaction with its owner "
                                                  "window until it is closed, often used for critical actions like "
                                                  "confirmations, authentication, or settings. Unlike a Dialog, which is a "
                                                  "lightweight pop-up within the same window, a modal window is a fully "
                                                  "independent window, making it suitable for multi-window applications or "
                                                  "scenarios requiring more flexibility in layout and behavior."),
                                        ui::Button{.text     = "Show modal window",
                                                   .margin   = ui::Edges{.top = 8.0f},
                                                   .horizontal_alignment  = ui::Alignment::Leading,
                                                   .on_click = [&ctx] { show_modal_window(ctx); }},
                                    },
                            }),
                            example({
                                .title   = "A full screen window",
                                .code    = R"cpp(
                                    ui::Window sample{{.initial_state = ui::WindowState::Fullscreen}};
                                    // A full screen window has no caption buttons, so give it a way out.
                                    sample.set_content(ui::VerticalStack{
                                        .spacing              = 8,
                                        .horizontal_alignment = ui::Alignment::Center,
                                        .vertical_alignment   = ui::Alignment::Center,
                                        .content = {
                                            ui::Text{
                                                .text = "This window is running in Fullscreen mode",
                                                .role = ui::TextRole::Title,
                                            },
                                            ui::Button{.text = "Close", .on_click = [&sample] { sample.close(); }},
                                        },
                                    });
                                    sample.show();
                                )cpp",
                                .stacked = true,
                                .controls =
                                    {
                                        paragraph("WindowState::Fullscreen makes a window cover the entire screen, removing "
                                                  "the title bar and system UI to create an immersive experience. To ensure "
                                                  "usability, an exit mechanism, such as handling the Escape key or close "
                                                  "button, should be included, and fullscreen mode should be used in scenarios "
                                                  "like media playback or focused tasks."),
                                        ui::Button{.text     = "Show window (Fullscreen mode)",
                                                   .horizontal_alignment  = ui::Alignment::Leading,
                                                   .on_click = [&ctx] { show_full_screen_window(ctx); }},
                                    },
                            }),
                            example({
                                .title   = "A picture-in-picture window",
                                .code    = R"cpp(
                                    // Always on top, no minimize or maximize button, and a 16:9 client area
                                    // covering a share of the work area: 5%, 15% or 25%.
                                    const float    share  = 0.15f;
                                    const ui::Rect area   = window.work_area().value_or(ui::Rect{0, 0, 1920, 1040});
                                    const float    width  = std::sqrt(share * area.width * area.height * 16.0f / 9.0f);
                                    const float    height = width * 9.0f / 16.0f;

                                    ui::Window overlay{{
                                        .size        = ui::Size{width, height},
                                        .style       = {.minimize_button = false, .maximize_button = false, .always_on_top = true},
                                    }};
                                    overlay.set_content(ui::Text{.text = "Picture-in-Picture"});
                                    overlay.show();
                                )cpp",
                                .stacked = true,
                                .options = options_panel(
                                    250.0f,
                                    {
                                        ui::ComboBox{.header               = "Size",
                                                     .items                = std::vector<std::string>(OverlaySizes.begin(), OverlaySizes.end()),
                                                     .selected_index       = 0,
                                                     .width                = 150.0f,
                                                     .horizontal_alignment              = ui::Alignment::Leading,
                                                     .on_selection_changed = [&s](std::optional<std::size_t> index) { s.overlay_size = index.value_or(0); }},
                                        ui::Text{.text     = ui::bind(s.overlay_size,
                                                              [](std::size_t index)
                                                              {
                                                                  return std::format("{}: Window size is approximately {} "
                                                                                     "of the display's work area.",
                                                                                     OverlaySizes[index],
                                                                                     OverlayShares[index]);
                                                              }),
                                                 .wrapping = ui::TextWrapping::Wrap},
                                    }),
                                .controls =
                                    {
                                        paragraph("A picture-in-picture window stays on top of other windows while using "
                                                  "minimal screen space. To ensure a good user experience, "
                                                  "the window should have a small yet functional size (e.g., for media players "
                                                  "or floating tools)."),
                                        ui::Button{.text     = "Show window (Picture-in-Picture mode)",
                                                   .horizontal_alignment  = ui::Alignment::Leading,
                                                   .on_click = [&ctx, &s] { show_compact_overlay_window(ctx, s.overlay_size.get()); }},
                                    },
                            }),
                        }),
                    },
            };
        });
}

// ---- Title bar customization ----

// A color selector: a SplitButton whose face shows the color (the -11,-6 margin
// cancels the padding) and whose secondary part opens a ColorPicker. The face has no click handler.
ui::Spec color_selector(const char* name, ui::Property<ui::Color>& color)
{
    return ui::Flyout{
        .placement = ui::PopupPlacement::Below,
        .anchor    = ui::SplitButton{.padding              = ui::Thickness{0.0f},
                                     .horizontal_alignment = ui::Alignment::Leading,
                                     .accessibility        = {.name = name},
                                     .content              = Swatch{.fill = color, .width = 64.0f, .height = 32.0f}},
        .content   = ui::ColorPicker{.color                            = color,
                                     .more_button_visible              = false,
                                     .color_channel_text_input_visible = false,
                                     .alpha_text_input_visible         = false,
                                     .hex_input_visible                = false},
    };
}

ui::Spec labeled(const char* label, ui::Spec control)
{
    return ui::VerticalStack{.spacing = 8.0f, .content = {ui::Text{.text = label}, std::move(control)}};
}

enum class TitleBarHeightOption
{
    Standard,
    Tall,
    Collapsed,
};
constexpr std::array<const char*, 3> HeightOptions{"Standard", "Tall", "Collapsed"};

enum class TitleBarTheme
{
    Legacy,
    UseDefaultAppMode,
    Light,
    Dark,
};
constexpr std::array<const char*, 4> TitleBarThemes{"Legacy", "App mode", "Light", "Dark"};

struct WindowTitleBarState
{
    // Color customization
    ui::Property<ui::Color> background{ui::Color::from_argb(0xFFF2F6FA)};
    ui::Property<ui::Color> foreground{ui::Color::from_argb(0xFF1E2933)};
    ui::Property<ui::Color> inactive_background{ui::Color::from_argb(0xFFE5EAF0)};
    ui::Property<ui::Color> inactive_foreground{ui::Color::from_argb(0xFF6B7280)};
    TrackedWindow           colors_window;

    // Extending content
    ui::Property<bool>        extend{true};
    ui::Property<std::size_t> height_option{0};
    TrackedWindow             extend_window;

    // Preferred theme
    ui::Property<std::size_t> theme{1};
    TrackedWindow             theme_window;

    // Declared last, so they go first: each pushes into a window above.
    std::vector<ui::Subscription> live;

    [[nodiscard]] ui::ChromeOverrides colors() const
    {
        return {.caption_color               = background.get(),
                .caption_text_color          = foreground.get(),
                .inactive_caption_color      = inactive_background.get(),
                .inactive_caption_text_color = inactive_foreground.get()};
    }
};

// Extending content into the title bar and the height option, as a WindowStyle: Tall is TitleBar's 48 DIP
// expanded height, Collapsed takes the caption buttons away with the caption.
ui::WindowStyle extended_style(bool extend, std::size_t option)
{
    ui::WindowStyle style{.resizable = false, .always_on_top = true, .extend_content_into_title_bar = extend};
    if (extend)
    {
        style.title_bar_height = option == static_cast<std::size_t>(TitleBarHeightOption::Tall) ? 48.0f : 0.0f;
        style.caption_buttons  = option != static_cast<std::size_t>(TitleBarHeightOption::Collapsed);
    }
    return style;
}

// The caption theme option as the caption's dark flag: Legacy is the light caption every window had
// before dark mode, App mode the user's app mode (not this app's theme), Light and Dark forced.
bool dark_caption(std::size_t theme)
{
    switch (static_cast<TitleBarTheme>(theme))
    {
        case TitleBarTheme::Legacy:
        case TitleBarTheme::Light:
            return false;
        case TitleBarTheme::UseDefaultAppMode:
            return ui::os_theme().dark;
        case TitleBarTheme::Dark:
            return true;
    }
    return false;
}

ui::Spec window_title_bar_page(Context& ctx)
{
    return with_state<WindowTitleBarState>(
        [&ctx](WindowTitleBarState& s)
        {
            // Every option applies to the window it belongs to while it is open.
            const auto push_colors = [&s](const ui::Color&)
            {
                if (s.colors_window.window != nullptr)
                {
                    s.colors_window.window->set_chrome(s.colors());
                }
            };
            for (ui::Property<ui::Color>* color :
                 {&s.background, &s.foreground, &s.inactive_background, &s.inactive_foreground})
            {
                s.live.push_back(color->observe(push_colors));
            }
            const auto push_extend = [&s](const auto&)
            {
                if (s.extend_window.window != nullptr)
                {
                    s.extend_window.window->set_style(extended_style(s.extend.get(), s.height_option.get()));
                }
            };
            s.live.push_back(s.extend.observe(push_extend));
            s.live.push_back(s.height_option.observe(push_extend));
            s.live.push_back(s.theme.observe(
                [&s](const std::size_t& theme)
                {
                    if (s.theme_window.window != nullptr)
                    {
                        s.theme_window.window->set_chrome({.dark_titlebar = dark_caption(theme)});
                    }
                }));

            const auto closed = [](bool open) { return !open; };
            return ui::VerticalStack{
                .spacing     = 8.0f,
                .child_alignment = ui::Alignment::Stretch,
                .content =
                    {
                        ui::Text{.inlines  = {{.text = "For the default title bar and basic scenarios, use the "},
                                              {.text = "TitleBar", .on_click = [&ctx] { ctx.navigate("TitleBar"); }},
                                              {.text = " control."}},
                                 .wrapping = ui::TextWrapping::Wrap},
                        examples({
                            example({
                                .title = "Title bar color customization",
                                .code  = R"cpp(
                                    ui::ChromeOverrides colors{
                                        .caption_color               = ui::Color::from_argb(0xFFF2F6FA),
                                        .caption_text_color          = ui::Color::from_argb(0xFF1E2933),
                                        .inactive_caption_color      = ui::Color::from_argb(0xFFE5EAF0),
                                        .inactive_caption_text_color = ui::Color::from_argb(0xFF6B7280),
                                    };

                                    ui::Window sample{{
                                        .title  = "Title bar colors",
                                        .size  = {600, 400},
                                        .style  = {.resizable = false, .always_on_top = true},
                                        .chrome = colors,
                                    }};
                                    sample.show();

                                    // Later, while the window is open:
                                    colors.caption_color = ui::colors::White;
                                    sample.set_chrome(colors);
                                )cpp",
                                .options =
                                    ui::Grid{.columns        = {ui::AutoSize, ui::AutoSize},
                                             .rows           = {ui::AutoSize},
                                             .column_spacing = 16.0f,
                                             .content =
                                                 {
                                                     ui::Cell{.row     = 0,
                                                      .column  = 0,
                                                      .content = ui::VerticalStack{.spacing  = 8.0f,
                                                                              .content = {
                                                                                  labeled("Caption color",
                                                                                           color_selector("Caption color", s.background)),
                                                                                  labeled("Caption text color",
                                                                                           color_selector("Caption text color", s.foreground)),
                                                                              }}},
                                                     ui::Cell{.row     = 0,
                                                      .column  = 1,
                                                      .content = ui::VerticalStack{.spacing  = 8.0f,
                                                                              .content = {
                                                                                  labeled("Inactive caption color",
                                                                                           color_selector("Inactive caption color",
                                                                                                          s.inactive_background)),
                                                                                  labeled("Inactive caption text color",
                                                                                           color_selector("Inactive caption text color",
                                                                                                          s.inactive_foreground)),
                                                                              }}},
                                                 }},
                                .controls =
                                    {
                                        ui::Button{.text     = "Show window",
                                                   .enabled  = ui::bind(s.colors_window.open, closed),
                                                   .on_click = [&ctx, &s]
                                                   {
                                                       s.colors_window.track(open_sample(
                                                           ctx,
                                                           {.title  = "Title bar colors",
                                                            .size  = {600, 400},
                                                            .style  = {.resizable = false, .always_on_top = true},
                                                            .chrome = s.colors()},
                                                           [](ui::Window&) -> ui::Spec
                                                           {
                                                               return sample_message("This is a sample window to demonstrate "
                                                                                     "title bar color customization.");
                                                           }));
                                                   }},
                                    },
                            }),
                            example({
                                .title   = "Extending content into the title bar area",
                                .code    = R"cpp(
                                    ui::WindowStyle style{
                                        .resizable                     = false,
                                        .always_on_top                 = true,
                                        .extend_content_into_title_bar = true,
                                        .title_bar_height              = 48,   // Tall; 0 is the standard height
                                        .caption_buttons               = true, // false collapses the caption
                                    };

                                    ui::Window sample{{
                                        .title  = "Extended title bar",
                                        .size  = {600, 400},
                                        .style  = style,
                                    }};
                                    // The content now reaches the top edge; a ui::DragArea as high as
                                    // sample.caption_insets() gives the window something to drag by.
                                    sample.show();

                                    // Later, while the window is open:
                                    style.extend_content_into_title_bar = false;
                                    sample.set_style(style);
                                )cpp",
                                .options = options_panel(
                                    200.0f,
                                    {
                                        ui::CheckBox{.text               = "Extend content into title bar",
                                                     .checked            = std::optional<bool>{true},
                                                     .margin             = ui::Edges{.bottom = 12.0f},
                                                     .on_checked_changed = [&s](std::optional<bool> on) { s.extend = on == true; }},
                                        ui::ComboBox{.header               = "Title bar height",
                                                     .items                = std::vector<std::string>(HeightOptions.begin(), HeightOptions.end()),
                                                     .selected_index       = 0,
                                                     .on_selection_changed = [&s](std::optional<std::size_t> index) { s.height_option = index.value_or(0); }},
                                    }),
                                .controls =
                                    {
                                        ui::Button{.text     = "Show window",
                                                   .enabled  = ui::bind(s.extend_window.open, closed),
                                                   .on_click = [&ctx, &s]
                                                   {
                                                       s.extend_window.track(open_sample(
                                                           ctx,
                                                           {.title  = "Extended title bar",
                                                            .size  = {600, 400},
                                                            .style  = extended_style(s.extend.get(), s.height_option.get())},
                                                           [](ui::Window& window) -> ui::Spec
                                                           {
                                                               return with_caption_band(
                                                                   window,
                                                                   sample_message("This is a sample window to demonstrate "
                                                                                  "content extending into the title bar area "
                                                                                  "and title bar height options."));
                                                           }));
                                                   }},
                                    },
                            }),
                            example({
                                .title   = "Title bar theme options",
                                .code    = R"cpp(
                                    // Light or dark by hand, or ui::os_theme().dark to follow the user's app mode.
                                    ui::Window sample{{
                                        .title  = "Title bar theme",
                                        .size  = {600, 400},
                                        .style  = {.resizable = false, .always_on_top = true},
                                        .chrome = {.dark_titlebar = ui::os_theme().dark},
                                    }};
                                    sample.show();

                                    // Later, while the window is open:
                                    sample.set_chrome({.dark_titlebar = true});
                                )cpp",
                                .options = options_panel(200.0f,
                                                         {
                                                             ui::ComboBox{.header         = "Caption theme",
                                                                          .items          = std::vector<std::string>(TitleBarThemes.begin(), TitleBarThemes.end()),
                                                                          .selected_index = std::size_t{1},
                                                                          .on_selection_changed =
                                                                              [&s](std::optional<std::size_t> index) { s.theme = index.value_or(0); }},
                                                         }),
                                .controls =
                                    {
                                        ui::Button{.text     = "Show window",
                                                   .enabled  = ui::bind(s.theme_window.open, closed),
                                                   .on_click = [&ctx, &s]
                                                   {
                                                       s.theme_window.track(open_sample(
                                                           ctx,
                                                           {.title  = "Title bar theme",
                                                            .size  = {600, 400},
                                                            .style  = {.resizable = false, .always_on_top = true},
                                                            .chrome = {.dark_titlebar = dark_caption(s.theme.get())}},
                                                           [](ui::Window&) -> ui::Spec
                                                           {
                                                               return sample_message("This is a sample window to demonstrate "
                                                                                     "title bar theme customization.");
                                                           }));
                                                   }},
                                    },
                            }),
                        }),
                    },
            };
        });
}

// ---- TitleBar ----

// The samples' title bar content: an AutoSuggestBox at most 580 wide, stretched and centered
// vertically, with "Search..." as its placeholder and a Find query icon.
ui::Spec title_bar_search()
{
    return ui::AutoSuggestBox{.placeholder_text   = "Search...",
                              .query_icon         = ui::symbol_glyph(ui::Symbol::Find, 16.0f),
                              .max_width          = 580.0f,
                              .vertical_alignment = ui::Alignment::Center};
}

ui::Spec title_bar_person()
{
    return ui::PersonPicture{.initials = "JD", .width = 30.0f, .height = 30.0f, .vertical_alignment = ui::Alignment::Center};
}

struct TitleBarConfigState
{
    ui::Property<std::string> title{"libwinui Gallery"};
    ui::Property<std::string> subtitle{"Preview"};
    ui::Property<bool>        back{false};
    ui::Property<bool>        pane_toggle{false};
};

// The drag regions window: a Tall extended title bar with a search box and a status badge whose
// drag-region setting the page below it sets.
struct DragRegionsState
{
    ui::Property<std::string>        status{};
    ui::Control<ui::Button>          badge;
    ui::Control<ui::HorizontalStack> header;
    ui::ButtonControl*               extra = nullptr;
};

ui::Spec drag_regions_window()
{
    return with_state<DragRegionsState>(
        [](DragRegionsState& s)
        {
            // Named, not nested: a Grid inside a Grid is expensive for MSVC to compile.
            ui::Spec header = ui::
                Grid{.columns        = {1_fr, ui::AutoSize},
                     .rows           = {1_fr},
                     .column_spacing = 8.0f,
                     .content          = {
                         ui::Cell{.row = 0, .column = 0, .content = title_bar_search()},
                         ui::Cell{.row    = 0,
                          .column = 1,
                          .content =
                              ui::HorizontalStack{
                                  .spacing            = 8.0f,
                                  .vertical_alignment = ui::Alignment::Center,
                                  .control            = s.header,
                                  .content = {ui::Button{.text               = "Status",
                                                         .variant            = ui::ButtonVariant::Accent,
                                                         .vertical_alignment = ui::Alignment::Center,
                                                         .on_click = [&s] { s.status = "Status badge clicked"; },
                                                         .control  = s.badge}},
                              }},
                     }};
            ui::Spec body = ui::VerticalStack{
                .spacing         = 16.0f,
                .child_alignment = ui::Alignment::Stretch,
                .margin          = ui::Symmetric{.horizontal = 32.0f, .vertical = 24.0f},
                .max_width       = 640.0f,
                .content =
                    {
                        ui::Text{.text = "Custom drag regions", .role = ui::TextRole::Subtitle},
                        ui::Text{.text     = "Try dragging the window from different parts of the title "
                                             "bar. Interactive controls (like the search box) are "
                                             "automatically excluded from the drag region.",
                                 .wrapping = ui::TextWrapping::WrapWholeWords},
                        ui::Text{.text = "Status badge", .role = ui::TextRole::BodyStrong},
                        ui::Text{.text     = "Pick a value for the badge in the title bar.",
                                 .wrapping = ui::TextWrapping::WrapWholeWords},
                        ui::RadioButtons{.items          = {"Unset (framework decides, clickable since Button "
                                                            "is interactive)",
                                                            "True (always draggable, overrides the framework "
                                                            "default)",
                                                            "False (always clickable)"},
                                         .selected_index = 0,
                                         .on_selection_changed =
                                             [&s](std::optional<std::size_t> index)
                                         {
                                             if (s.badge == nullptr)
                                             {
                                                 return;
                                             }
                                             s.badge->set_title_bar_drag_region(
                                                 index == 0 ? std::nullopt : std::optional<bool>{index == 1});
                                         }},
                        ui::Text{.text = "Dynamic content", .role = ui::TextRole::BodyStrong},
                        ui::Text{.text     = "Elements added to or removed from the TitleBar's content at runtime "
                                             "take part at once: the title bar is asked on every press, so "
                                             "there are no drag regions to recompute.",
                                 .wrapping = ui::TextWrapping::WrapWholeWords},
                        ui::HorizontalStack{
                            .spacing = 8.0f,
                            .content = {ui::Button{.text = "Toggle extra title bar button",
                                                   .on_click =
                                                       [&s]
                                                   {
                                                       if (s.extra == nullptr)
                                                       {
                                                           auto extra = std::make_unique<ui::ButtonControl>("Extra");
                                                           extra->set_vertical_alignment(ui::Alignment::Center);
                                                           s.extra  = s.header->insert(0, std::move(extra));
                                                           s.status = "Added a Button to the TitleBar's content.";
                                                       }
                                                       else
                                                       {
                                                           s.header->remove(s.extra);
                                                           s.extra  = nullptr;
                                                           s.status = "Removed the Button.";
                                                       }
                                                   }}}},
                        ui::Text{.text = s.status, .wrapping = ui::TextWrapping::WrapWholeWords},
                    },
            };
            return ui::Grid{
                .columns = {1_fr},
                .rows    = {ui::AutoSize, 1_fr},
                .content =
                    {
                        ui::Cell{.row    = 0,
                                 .column = 0,
                                 .content =
                                     ui::TitleBar{
                                         .title             = "Drag regions",
                                         .subtitle          = "Try dragging the window",
                                         .icon              = gallery_icon(),
                                         .content_alignment = ui::Alignment::Stretch,
                                         .content           = std::move(header),
                                     }},
                        ui::Cell{.row = 1, .column = 0, .content = ui::Scroll{.content = std::move(body)}},
                    },
            };
        });
}

// The end to end window: a TitleBar over a NavigationShell, the back button following the frame's back
// stack and the pane toggle opening the pane.
struct EndToEndState
{
    ui::Property<bool>               can_go_back{false};
    ui::Control<ui::NavigationShell> nav;
    ui::Control<ui::NavigationFrame> frame;
};

ui::Spec end_to_end_window()
{
    return with_state<EndToEndState>(
        [](EndToEndState& s)
        {
            ui::Spec view = ui::Grid{
                .columns = {1_fr},
                .rows    = {ui::AutoSize, 1_fr},
                .content =
                    {
                        ui::Cell{.row     = 0,
                         .column  = 0,
                         .content = ui::TitleBar{
                             .title                      = "libwinui Gallery",
                             .subtitle                   = "TitleBar sample",
                             .icon                       = gallery_icon(),
                             .back_button_visible        = s.can_go_back,
                             .pane_toggle_button_visible = true,
                             .content_alignment          = ui::Alignment::Stretch,
                             .on_back_requested          = [&s]
                             {
                                 if (s.frame != nullptr)
                                 {
                                     s.frame->go_back();
                                 }
                             },
                             .on_pane_toggle_requested   = [&s]
                             {
                                 if (s.nav != nullptr)
                                 {
                                     s.nav->set_pane_open(!s.nav->is_pane_open());
                                 }
                             },
                             .content      = title_bar_search(),
                             .right_header_content = title_bar_person(),
                         }},
                        ui::Cell{.row     = 1,
                         .column  = 0,
                         .content = ui::NavigationShell{
                             .settings_visible              = false,
                             .back_button_visible           = ui::AutoVisibility::Collapsed,
                             .pane_toggle_button_visible    = false,
                             .on_selection_changed          = [&s](const std::string& key)
                             {
                                 const std::optional<std::size_t> index = s.nav != nullptr ? s.nav->index_of(key) : std::nullopt;
                                 if (s.frame == nullptr || !index || *index > 3)
                                 {
                                     return;
                                 }
                                 s.nav->set_header(std::format("Sample Page {}", *index + 1));
                                 s.frame->navigate([page = static_cast<int>(*index) + 1]
                                                   { return ui::build(navigation_sample_page(page)); });
                             },
                             .control     = s.nav,
                             .content = ui::NavigationFrame{.on_navigated = [&s](const ui::NavigationEventArgs&)
                                                  { s.can_go_back = s.frame != nullptr && s.frame->can_go_back(); },
                                                  .control = s.frame},
                             .items =
                                 {
                                     ui::NavItem{.text = "Menu Item1", .icon = ui::symbol_glyph(ui::Symbol::Play)},
                                     ui::NavItem{.text = "Menu Item2", .icon = ui::symbol_glyph(ui::Symbol::Save)},
                                     ui::NavItem{.text = "Menu Item3", .icon = ui::symbol_glyph(ui::Symbol::Refresh)},
                                     ui::NavItem{.text = "Menu Item4", .icon = ui::symbol_glyph(ui::Symbol::Download)},
                                 },
                         }},
                    },
            };
            // Select the first item once everything it navigates exists. The view
            // starts on it without a selection event, so it is cleared and selected again.
            s.nav->set_selected_key({});
            s.nav->set_selected_key("Menu Item1");
            return view;
        });
}

ui::Spec title_bar_page(Context& ctx)
{
    return with_state<TitleBarConfigState>(
        [&ctx](TitleBarConfigState& s)
        {
            const auto window_card =
                [&ctx](const char* title, const char* text, const char* window_title, const char* code, auto build)
            {
                return example({
                    .title   = title,
                    .code    = code,
                    .stretch = true,
                    .controls =
                        {
                            ui::VerticalStack{.spacing   = 12.0f,
                                              .max_width = 560.0f,
                                              .content =
                                                  {
                                                      ui::Text{.text                 = text,
                                                               .wrapping             = ui::TextWrapping::WrapWholeWords,
                                                               .text_alignment       = ui::TextAlignment::Center,
                                                               .horizontal_alignment = ui::Alignment::Center},
                                                      ui::Button{.text                 = "Show window",
                                                                 .variant              = ui::ButtonVariant::Accent,
                                                                 .horizontal_alignment = ui::Alignment::Center,
                                                                 .on_click =
                                                                     [&ctx, window_title, build]
                                                                 {
                                                                     open_sample(ctx,
                                                                                 {.title = window_title,
                                                                                  .style = {.extend_content_into_title_bar = true,
                                                                                            .title_bar_height = 48}},
                                                                                 build);
                                                                 }},
                                                  }},
                        },
                });
            };
            return ui::VerticalStack{
                .spacing     = 8.0f,
                .child_alignment = ui::Alignment::Stretch,
                .content =
                    {
                        ui::RichText{.blocks = {{.inlines = {{.text = "For full title bar customization without using the "
                                                                      "TitleBar control, see the "},
                                                             {.text     = "Window Title Bar",
                                                              .on_click = [&ctx] { ctx.navigate("Window Title Bar"); }},
                                                             {.text = " sample"}}}},
                                     .selection_enabled = false},
                        examples({
                            example({
                                .title   = "TitleBar configuration",
                                .code    = R"cpp(
                                    ui::Property<std::string> title{"libwinui Gallery"};
                                    ui::Property<std::string> subtitle{"Preview"};
                                    ui::Property<bool>        back{false};
                                    ui::Property<bool>        pane_toggle{false};

                                    // icon is the app's ui::Bitmap.
                                    ui::TitleBar{
                                        .title                      = title,
                                        .subtitle                   = subtitle,
                                        .icon                       = icon,
                                        .back_button_visible        = back,
                                        .pane_toggle_button_visible = pane_toggle,
                                        .content_alignment          = ui::Alignment::Stretch,
                                        .content = ui::AutoSuggestBox{
                                            .placeholder_text   = "Search...",
                                            .query_icon         = ui::symbol_glyph(ui::Symbol::Find, 16.0f),
                                            .max_width          = 580,
                                            .vertical_alignment = ui::Alignment::Center,
                                        },
                                        .right_header_content = ui::PersonPicture{
                                            .initials           = "JD",
                                            .width               = 30,
                                            .height              = 30,
                                            .vertical_alignment = ui::Alignment::Center,
                                        },
                                    }
                                )cpp",
                                .stretch = true,
                                .options = options_panel(240.0f,
                                                         {
                                                             ui::TextBox{.header          = "Title",
                                                                         .text            = s.title.get(),
                                                                         .on_text_changed = [&s](const std::string& text) { s.title = text; }},
                                                             ui::TextBox{.header          = "Subtitle",
                                                                         .text            = s.subtitle.get(),
                                                                         .on_text_changed = [&s](const std::string& text) { s.subtitle = text; }},
                                                             ui::ToggleSwitch{.header     = "Back button",
                                                                              .on_checked_changed = [&s](bool on) { s.back = on; }},
                                                             ui::ToggleSwitch{.header     = "Pane toggle button",
                                                                              .on_checked_changed = [&s](bool on) { s.pane_toggle = on; }},
                                                         }),
                                .controls =
                                    {
                                        themed_box(BoxStyle::Surface,
                                                   ui::Thickness{0.0f},
                                                   ui::TitleBar{
                                                       .title                      = s.title,
                                                       .subtitle                   = s.subtitle,
                                                       .icon                       = gallery_icon(),
                                                       .back_button_visible        = s.back,
                                                       .pane_toggle_button_visible = s.pane_toggle,
                                                       .content_alignment          = ui::Alignment::Stretch,
                                                       .content                    = title_bar_search(),
                                                       .right_header_content               = title_bar_person(),
                                                   }),
                                    },
                            }),
                            window_card("TitleBar drag regions",
                                        "Drag regions can only be observed on a real window. Click the button below to open "
                                        "a sample window where you can set a status badge's title bar drag region and add "
                                        "a button to the title bar while it runs.",
                                        "TitleBar drag regions sample",
                                        R"cpp(
                                            ui::Window sample{{
                                                .title = "TitleBar drag regions sample",
                                                .style = {.extend_content_into_title_bar = true, .title_bar_height = 48},
                                            }};

                                            ui::Control<ui::Button> badge;

                                            // The first row of the window's content. The bar drags the window;
                                            // interactive controls in it, like the search box, stay clickable.
                                            ui::TitleBar{
                                                .title             = "Drag regions",
                                                .subtitle          = "Try dragging the window",
                                                .content_alignment = ui::Alignment::Stretch,
                                                .content = ui::HorizontalStack{
                                                    .spacing = 8,
                                                    .content = {
                                                        ui::AutoSuggestBox{.placeholder_text = "Search..."},
                                                        ui::Button{
                                                            .text    = "Status",
                                                            .variant = ui::ButtonVariant::Accent,
                                                            .control = badge,
                                                        },
                                                    },
                                                },
                                            }

                                            // true: the badge drags the window. false: it is always clickable.
                                            // std::nullopt: the control decides, and a button is clickable.
                                            badge->set_title_bar_drag_region(true);
                                        )cpp",
                                        [](ui::Window&) { return drag_regions_window(); }),
                            window_card("End to end TitleBar sample",
                                        "Click the button below to see an end to end sample of a TitleBar in an new window, "
                                        "binding some of its properties to the NavigationShell and navigation frame.",
                                        "libwinui Gallery",
                                        R"cpp(
                                            ui::Window sample{{
                                                .title = "libwinui Gallery",
                                                .style = {.extend_content_into_title_bar = true, .title_bar_height = 48},
                                            }};

                                            ui::Property<bool>               can_go_back{false};
                                            ui::Control<ui::NavigationShell>  nav;
                                            ui::Control<ui::NavigationFrame> frame;

                                            // Row 0 of the window's content, above the NavigationShell.
                                            ui::TitleBar{
                                                .title                      = "libwinui Gallery",
                                                .subtitle                   = "TitleBar sample",
                                                .back_button_visible        = can_go_back,
                                                .pane_toggle_button_visible = true,
                                                .on_back_requested          = [&] { frame->go_back(); },
                                                .on_pane_toggle_requested   = [&] { nav->set_pane_open(!nav->is_pane_open()); },
                                                .content                    = ui::AutoSuggestBox{.placeholder_text = "Search..."},
                                                .right_header_content               = ui::PersonPicture{.initials = "JD"},
                                            }

                                            // Row 1: the title bar has the back and pane buttons, so the view hides its own.
                                            ui::NavigationShell{
                                                .back_button_visible           = ui::AutoVisibility::Collapsed,
                                                .pane_toggle_button_visible    = false,
                                                .control                       = nav,
                                                .content = ui::NavigationFrame{
                                                    .on_navigated = [&](const ui::NavigationEventArgs&)
                                                    { can_go_back = frame->can_go_back(); },
                                                    .control      = frame,
                                                },
                                                .items = {
                                                    ui::NavItem{.text = "Menu Item1", .icon = ui::symbol_glyph(ui::Symbol::Play)},
                                                    ui::NavItem{.text = "Menu Item2", .icon = ui::symbol_glyph(ui::Symbol::Save)},
                                                },
                                            }
                                        )cpp",
                                        [](ui::Window&) { return end_to_end_window(); }),
                        }),
                    },
            };
        });
}

// ---- Window States ----

std::string state_text(ui::WindowState state)
{
    switch (state)
    {
        case ui::WindowState::Normal:
            return "This window is in its normal state.";
        case ui::WindowState::Minimized:
            return "This window is minimized.";
        case ui::WindowState::Maximized:
            return "This window is maximized.";
        case ui::WindowState::Fullscreen:
            return "This window is full screen.";
    }
    return {};
}

// Window::state() is written only by the host, so the label below follows the caption buttons,
// Win+arrows and the taskbar just as it follows these buttons.
ui::Spec window_states_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    auto&       status = ctx.status;
    return examples({
        example("Ask for a state; the label shows the one the window is actually in",
                {
                    ui::Text{.text = ui::bind(window.state(), state_text)},
                    ui::Button{.text     = "Minimize",
                               .on_click = [&window] { window.request_state(ui::WindowState::Minimized); }},
                    ui::Button{.text     = "Maximize",
                               .on_click = [&window] { window.request_state(ui::WindowState::Maximized); }},
                    ui::Button{.text = "Restore", .on_click = [&window] { window.restore(); }},
                },
                false,
                R"cpp(
                    // state() is written by the window itself, so the label also follows
                    // the caption buttons, Win+arrows and the taskbar.
                    ui::HorizontalStack{
                        .spacing = 8,
                        .content = {
                            ui::Text{
                                .text = ui::bind(window.state(), [](ui::WindowState state)
                                {
                                    return std::string{state == ui::WindowState::Maximized ? "Maximized" : "Not maximized"};
                                }),
                            },
                            ui::Button{
                                .text     = "Minimize",
                                .on_click = [&window] { window.request_state(ui::WindowState::Minimized); },
                            },
                            ui::Button{
                                .text     = "Maximize",
                                .on_click = [&window] { window.request_state(ui::WindowState::Maximized); },
                            },
                            ui::Button{.text = "Restore", .on_click = [&window] { window.restore(); }},
                        },
                    }
                )cpp"),
        example("Full screen covers the whole monitor; Restore returns to where it was entered from",
                {
                    ui::Button{.text    = ui::bind(window.state(),
                                                   [](ui::WindowState s)
                                                   {
                                                    return std::string{s == ui::WindowState::Fullscreen
                                                                           ? "Exit full "
                                                                             "screen"
                                                                           : "Full "
                                                                             "screen"};
                                                   }),
                               .variant = ui::ButtonVariant::Accent,
                               .on_click =
                                   [&window, &status]
                               {
                                   window.toggle_fullscreen();
                                   status = "Window: full screen toggled";
                               }},
                },
                false,
                R"cpp(
                    ui::Button{
                        .text = ui::bind(window.state(), [](ui::WindowState state)
                        {
                            return std::string{state == ui::WindowState::Fullscreen ? "Exit full screen" : "Full screen"};
                        }),
                        .variant  = ui::ButtonVariant::Accent,
                        .on_click = [&window] { window.toggle_fullscreen(); },
                    }
                )cpp"),
    });
}

} // namespace

std::span<const Page> windowing_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Window", "Centered, size-limited, modal, full screen and compact overlay windows.", window_page},
        {"Window Title Bar", "The colors, height and content of a window's own title bar.", window_title_bar_page},
        {"TitleBar", "An example showing how to use the default TitleBar control.", title_bar_page},
        {"Windowing", "Create top-level windows and configure their client-area size.", windowing_page},
        {"Window States", "Minimize, maximize, full screen and restore, observed through Window::state().", window_states_page},
    });
    return Pages;
}

} // namespace gallery

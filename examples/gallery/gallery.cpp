// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include "winui/render/Chrome.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cmath>
#include <cstdint>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// A small fixed step per elevation, like the Settings app. A percentage blend toward white
// barely moves #202020 and overshoots badly further up.
constexpr float SurfaceStep = 6.0f / 255.0f;
constexpr float CardStep    = 14.0f / 255.0f;
constexpr float EdgeStep    = 12.0f / 255.0f;

constexpr ui::Color shift(const ui::Color& c, float amount)
{
    return {std::clamp(c.r + amount, 0.0f, 1.0f),
            std::clamp(c.g + amount, 0.0f, 1.0f),
            std::clamp(c.b + amount, 0.0f, 1.0f),
            c.a};
}

// A visible step away from `c`: darker on a light theme, where every lighter step is already
// clamped at white (the card stroke is #E5E5E5 on a white card).
constexpr ui::Color edge(const ui::Color& c, float amount)
{
    const float luminance = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
    return shift(c, luminance > 0.5f ? -amount : amount);
}

// A negative radius means the theme's own control radius, so era themes keep their square
// corners in the gallery chrome too.
constexpr float resolved_radius(float radius, const ui::Theme& theme)
{
    return radius >= 0.0f ? radius : theme.corner_radius;
}

// A single child inset by padding, for the painted containers below. Padding, because a card
// must be bigger than its content, and margin would shrink the painted rect onto the child.
class Padded : public ui::ContentHost
{
public:
    Padded(std::unique_ptr<ui::Element> child, ui::Thickness padding) : padding_(padding)
    {
        set_child(std::move(child));
    }

protected:
    ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
    {
        const ui::Size inner_available{std::max(0.0f, available.width - padding_.horizontal()),
                                       std::max(0.0f, available.height - padding_.vertical())};
        const ui::Size inner = ui::ContentHost::measure_override(inner_available, context);
        return ui::Size{inner.width + padding_.horizontal(), inner.height + padding_.vertical()};
    }

    void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext& context) override
    {
        ui::ContentHost::arrange_override(final_rect.inset(padding_), context);
    }


private:
    ui::Thickness padding_;
};

// Derived from the current theme's colors.background every frame, so cards follow theme switches.
class Card final : public Padded
{
public:
    explicit Card(std::unique_ptr<ui::Element> child, float radius = -1.0f, ui::Thickness padding = ui::Thickness{16.0f}) :
        Padded(std::move(child), padding),
        radius_(radius)
    {
    }

protected:
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme  = context.theme;
        const float      radius = resolved_radius(radius_, theme);
        const ui::Color  fill   = shift(theme.colors().background, CardStep);
        renderer.fill_rounded_rect(bounds(), radius, fill);
        renderer.stroke_rounded_rect(bounds(), radius, edge(fill, EdgeStep), 1.0f);
        Padded::render_override(renderer, context);
    }


private:
    float radius_;
};

// The example card's options pane, a divider on the left edge. Full card height, so it
// sits flush against the card's edges rather than inside its padding.
class OptionsPane final : public Padded
{
public:
    explicit OptionsPane(std::unique_ptr<ui::Element> child) : Padded(std::move(child), ui::Thickness{16.0f})
    {
    }

protected:
    ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
    {
        desired_ = Padded::measure_override(available, context);
        return desired_;
    }

    // Top-aligned, so a lone ComboBox does not grow with a tall example.
    // Only the child is shortened; the divider keeps the row's height.
    void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext& context) override
    {
        Padded::arrange_override(ui::Rect{final_rect.x,
                                          final_rect.y,
                                          final_rect.width,
                                          std::min(desired_.height, final_rect.height)},
                                 context);
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        const ui::Color  card  = shift(theme.colors().background, CardStep);
        renderer.stroke_line(ui::Point{bounds().x + 0.5f, bounds().y},
                             ui::Point{bounds().x + 0.5f, bounds().y + bounds().height},
                             edge(card, EdgeStep),
                             1.0f);
        Padded::render_override(renderer, context);
    }


private:
    mutable ui::Size desired_{};
};

// Clipped to its bounds. Otherwise a control wider than the star column paints over the
// options panel.
class ControlsPane final : public Padded
{
public:
    explicit ControlsPane(std::unique_ptr<ui::Element> child) : Padded(std::move(child), ui::Thickness{16.0f})
    {
    }

protected:
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        renderer.push_clip(bounds());
        Padded::render_override(renderer, context);
        renderer.pop_clip();
    }
};

class OutputPane final : public Padded
{
public:
    explicit OutputPane(std::unique_ptr<ui::Element> child) : Padded(std::move(child), ui::Thickness{16.0f})
    {
    }

protected:
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        renderer.fill_rounded_rect(bounds(), theme.corner_radius, shift(theme.colors().background, SurfaceStep));
        Padded::render_override(renderer, context);
    }
};

// The card's control slot: measures one control against the card width, so a wrapping
// control wraps there instead of getting a row's unbounded width.
class Presenter final : public ui::ContentHost
{
public:
    Presenter(std::unique_ptr<ui::Element> child, bool stretch) : stretch_(stretch)
    {
        set_child(std::move(child));
    }

protected:
    ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
    {
        desired_child_ = ui::ContentHost::measure_override(available, context);
        return desired_child_;
    }

    void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext& context) override
    {
        // Always Top, the slot's default. Stretched, a control would grow with the options column.
        const float width  = stretch_ ? final_rect.width : std::min(desired_child_.width, final_rect.width);
        const float height = std::min(desired_child_.height, final_rect.height);
        ui::ContentHost::arrange_override(ui::Rect{final_rect.x, final_rect.y, width, height}, context);
    }


private:
    bool             stretch_;
    mutable ui::Size desired_child_{};
};

class SwatchElement final : public ui::Element
{
public:
    SwatchElement(float width, float height, float border_thickness) :
        width_(width),
        height_(height),
        border_thickness_(border_thickness)
    {
    }

    void set_fill(ui::Color color)
    {
        fill_ = color;
        mark_dirty(ui::DirtyFlags::Visual);
    }

    void set_border(ui::Color color)
    {
        border_ = color;
        mark_dirty(ui::DirtyFlags::Visual);
    }

protected:
    ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        // A zero width measures as nothing and lets the default Stretch alignment widen it.
        return ui::Size{width_, height_};
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext&) const override
    {
        renderer.fill_rounded_rect(bounds(), 0.0f, fill_);
        if (border_thickness_ > 0.0f)
        {
            const float half = border_thickness_ * 0.5f;
            renderer.stroke_rounded_rect(bounds().inset(ui::Thickness{half}), 0.0f, border_, border_thickness_);
        }
    }


private:
    float     width_;
    float     height_;
    float     border_thickness_;
    ui::Color fill_   = ui::colors::Transparent;
    ui::Color border_ = ui::colors::Transparent;
};

// Only the top-left corner is rounded. The Renderer has no per-corner radius, so the rect
// grows past the right and bottom edges inside a clip.
class Surface final : public ui::ContentHost
{
public:
    explicit Surface(std::unique_ptr<ui::Element> child, float radius = -1.0f) : radius_(radius)
    {
        set_child(std::move(child));
    }

protected:
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        // A NavigationShell that paints its own content area (Fluent 2,
        // Theme::Surfaces::nav_content) already is this surface.
        if (theme.surfaces.nav_content)
        {
            ui::ContentHost::render_override(renderer, context);
            return;
        }
        const float radius = resolved_radius(radius_, theme);
        renderer.push_clip(bounds());
        renderer.fill_rounded_rect(ui::Rect{bounds().x, bounds().y, bounds().width + radius, bounds().height + radius},
                                   radius,
                                   shift(theme.colors().background, SurfaceStep));
        renderer.pop_clip();
        ui::ContentHost::render_override(renderer, context);
    }


private:
    float radius_;
};

// The Popup sample's child: acrylic fill, surface stroke, the overlay corner radius.
class OverlayCard final : public Padded
{
public:
    OverlayCard(std::unique_ptr<ui::Element> child, ui::Thickness padding) : Padded(std::move(child), padding)
    {
    }

protected:
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme  = context.theme;
        const ui::Color  fill   = theme.surfaces.menu;
        const ui::Color  stroke = theme.surfaces.surface_stroke.value_or(
            theme.surfaces.menu_border.value_or(theme.surfaces.field_border));
        renderer.fill_rounded_rect(bounds(), theme.overlay_corner_radius, fill);
        ui::stroke_rounded_rect_inside(renderer, bounds(), theme.overlay_corner_radius, stroke, theme.border_width);
        // The card's colors, not the page's.
        const ui::Theme inside = theme.with_content_colors(theme.surfaces.menu_text, fill);
        Padded::render_override(renderer, context.with_theme(inside));
    }
};

class ThemedBox final : public Padded
{
public:
    ThemedBox(std::unique_ptr<ui::Element> child, BoxStyle style, ui::Thickness padding) :
        Padded(std::move(child), padding),
        style_(style)
    {
    }

protected:
    ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
    {
        return Padded::measure_override(available, context.with_theme(inside(context.theme)));
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme&   theme = context.theme;
        const ui::Palette& p     = theme.palette;
        const bool fluent = (theme.variant == ui::ThemeVariant::Fluent2 || theme.variant == ui::ThemeVariant::Fluent) &&
                            !theme.high_contrast;
        std::optional<ui::Color> fill;
        ui::Color                stroke = fluent ? p.control_stroke_default : theme.surfaces.field_border;
        switch (style_)
        {
            case BoxStyle::Card:
                fill   = fluent ? p.card_background_secondary : theme.colors().field;
                stroke = fluent ? p.card_stroke_default : theme.surfaces.field_border;
                break;
            case BoxStyle::Surface:
                fill   = fluent ? p.card_background_default : theme.colors().field;
                stroke = fluent ? p.card_stroke_default : theme.surfaces.field_border;
                break;
            case BoxStyle::Control:
                fill = fluent ? p.control_fill_default : theme.colors().control_fill;
                break;
            case BoxStyle::Accent:
                fill = theme.colors().accent;
                break;
            case BoxStyle::Frame:
                stroke = fluent ? p.card_stroke_default : theme.surfaces.field_border;
                break;
            case BoxStyle::Subtle:
                fill = fluent ? p.subtle_fill_tertiary : theme.colors().field;
                break;
        }
        const float edge = style_ == BoxStyle::Subtle ? 0.0f : 1.0f;
        ui::draw_border(renderer,
                        bounds(),
                        fill,
                        stroke,
                        ui::Thickness{edge},
                        style_ == BoxStyle::Surface ? 0.0f : theme.corner_radius);
        Padded::render_override(renderer, context.with_theme(inside(theme)));
    }


private:
    [[nodiscard]] ui::Theme inside(const ui::Theme& theme) const
    {
        if (style_ != BoxStyle::Accent)
        {
            return theme;
        }
        return theme.with_content_colors(theme.colors().text_on_accent);
    }

    BoxStyle style_;
};

// An originally authored stand-in for an animated icon: a circle growing from a point with
// progress. render_frame() is a pure function of progress, so both pages can share it.
class PulseVisual final : public ui::AnimatedIconSource
{
public:
    explicit PulseVisual(ui::Color color) : color_(color)
    {
        markers_["Normal"]      = 0.0;
        markers_["PointerOver"] = 0.5;
        markers_["Pressed"]     = 1.0;
    }

    [[nodiscard]] ui::Size natural_size() const override
    {
        return ui::Size{24.0f, 24.0f};
    }
    [[nodiscard]] ui::Duration duration() const override
    {
        return ui::Duration{900.0};
    }
    void render_frame(ui::Renderer& renderer, const ui::Rect& bounds, double progress) const override
    {
        const ui::Point center     = bounds.center();
        const float     max_radius = std::min(bounds.width, bounds.height) * 0.5f;
        const float     radius     = max_radius * static_cast<float>(std::clamp(progress, 0.0, 1.0));
        if (radius > 0.0f)
        {
            renderer.fill_ellipse(center, radius, radius, color_);
        }
    }
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override
    {
        return markers_;
    }

private:
    ui::Color                               color_;
    std::unordered_map<std::string, double> markers_;
};

} // namespace

namespace
{
// A snippet as it is written in the source, a raw string indented with the code around it, as it
// should read on screen: without the blank lines at its ends and without the shared indentation.
std::string dedent(std::string_view code)
{
    std::vector<std::string_view> lines;
    for (std::size_t at = 0; at <= code.size();)
    {
        const std::size_t end = std::min(code.find('\n', at), code.size());
        lines.push_back(code.substr(at, end - at));
        at = end + 1;
    }
    const auto blank = [](std::string_view line) { return line.find_first_not_of(" \t\r") == std::string_view::npos; };
    while (!lines.empty() && blank(lines.front()))
    {
        lines.erase(lines.begin());
    }
    while (!lines.empty() && blank(lines.back()))
    {
        lines.pop_back();
    }
    std::size_t indent = std::string_view::npos;
    for (std::string_view line : lines)
    {
        if (!blank(line))
        {
            indent = std::min(indent, line.find_first_not_of(' '));
        }
    }
    std::string out;
    for (std::string_view line : lines)
    {
        if (!out.empty())
        {
            out += '\n';
        }
        if (!blank(line))
        {
            line.remove_prefix(std::min(indent, line.size()));
            while (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            out += line;
        }
    }
    return out;
}

// The "Source code" expander under a card: the snippet in a fixed-pitch face, selectable, so it
// can be copied with Ctrl+C.
ui::Spec source_code(std::string_view code)
{
    return ui::Expander{
        .header = "Source code",
        .content =
            ui::Scroll{
                .content_orientation = ui::ScrollContentOrientation::Horizontal,
                .content             = ui::Text{.text              = dedent(code),
                                                .selection_enabled = true,
                                                .text_style        = ui::TextStyle{.font_size   = 13.0f,
                                                                                   .horizontal  = ui::TextAlign::Leading,
                                                                                   .vertical    = ui::TextAlign::Leading,
                                                                                   .font_family = "Cascadia Mono"}},
            },
    };
}
} // namespace

ui::Spec example(Example card)
{
    // A row centers its controls; Stack's Stretch default would grow them to the tallest.
    // A single control skips the row, which would give it unbounded width and stop it wrapping.
    std::unique_ptr<ui::Element> controls;
    if (card.stacked)
    {
        controls = ui::build(ui::VerticalStack{.spacing         = 12.0f,
                                               .child_alignment = ui::Alignment::Stretch,
                                               .content         = std::move(card.controls)});
    }
    else if (card.controls.nodes.size() == 1)
    {
        controls = std::make_unique<Presenter>(std::move(card.controls.nodes.front()), card.stretch);
    }
    else
    {
        controls = ui::build(ui::HorizontalStack{.spacing         = 16.0f,
                                                 .child_alignment = ui::Alignment::Center,
                                                 .content         = std::move(card.controls)});
    }

    ui::CellList cells;
    cells.cells.push_back({0, 0, 1, 1, std::make_unique<ControlsPane>(std::move(controls))});
    if (card.output)
    {
        auto output = std::make_unique<OutputPane>(ui::build(ui::VerticalStack{
            .spacing         = 4.0f,
            .child_alignment = ui::Alignment::Stretch,
            .content = {ui::Text{.text = "Output:", .horizontal_alignment = ui::Alignment::Leading}, std::move(card.output)},
        }));
        output->set_margin(ui::Edges{.top = 12.0f, .right = 12.0f, .bottom = 12.0f});
        output->set_min_width(160.0f);
        cells.cells.push_back({0, 1, 1, 1, std::move(output)});
    }
    if (card.options)
    {
        cells.cells.push_back({0, 2, 1, 1, std::make_unique<OptionsPane>(std::move(card.options).build())});
    }

    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .content =
            {
                ui::Text{.text                 = std::move(card.title),
                         .role                 = ui::TextRole::Subtitle,
                         .horizontal_alignment = ui::Alignment::Leading},
                std::make_unique<Card>(ui::build(ui::Grid{.columns = {1_fr, ui::AutoSize, ui::AutoSize},
                                                          .rows    = {ui::AutoSize},
                                                          .content = std::move(cells)}),
                                       -1.0f,
                                       ui::Thickness{0.0f}),
                ui::when(!card.code.empty(), source_code(card.code)),
            },
    };
}

ui::Spec example(std::string caption, ui::SpecList controls, bool stacked, std::string code)
{
    return example(
        {.title = std::move(caption), .code = std::move(code), .stacked = stacked, .controls = std::move(controls)});
}

ui::Spec examples(ui::SpecList cards)
{
    return ui::VerticalStack{.spacing = 24.0f, .child_alignment = ui::Alignment::Stretch, .content = std::move(cards)};
}

std::unique_ptr<ui::Element> Swatch::build() &&
{
    auto           node = std::make_unique<SwatchElement>(width, height, border_thickness);
    SwatchElement* raw  = node.get();
    // A fixed size is the element's own Width/Height too, so a Stretch slot centers the swatch
    // rather than widening it, as a fixed-size Rectangle does.
    if (width > 0.0f)
    {
        node->set_width(width);
    }
    if (height > 0.0f)
    {
        node->set_height(height);
    }
    ui::apply(fill, *node, [raw](const ui::Color& c) { raw->set_fill(c); });
    ui::apply(border, *node, [raw](const ui::Color& c) { raw->set_border(c); });
    return node;
}

std::unique_ptr<ui::Element> content_surface(ui::Spec content)
{
    return std::make_unique<Surface>(std::move(content).build());
}

std::unique_ptr<ui::Element> themed_box(BoxStyle style, ui::Thickness padding, ui::Spec content)
{
    return std::make_unique<ThemedBox>(std::move(content).build(), style, padding);
}

std::unique_ptr<ui::Element> overlay_card(ui::Spec content, ui::Thickness padding)
{
    return std::make_unique<OverlayCard>(std::move(content).build(), padding);
}

const ui::Bitmap& placeholder_photo()
{
    static const ui::Bitmap photo = []
    {
        constexpr int Size = 64;
        ui::Bitmap    img(Size, Size);
        for (int y = 0; y < Size; ++y)
        {
            for (int x = 0; x < Size; ++x)
            {
                const float t = static_cast<float>(x + y) / (2.0f * (Size - 1));
                img.set(x,
                        y,
                        ui::Color::from_rgba(static_cast<std::uint8_t>(30.0f + t * 40.0f),
                                             static_cast<std::uint8_t>(90.0f + t * 100.0f),
                                             static_cast<std::uint8_t>(160.0f + t * 80.0f),
                                             255));
            }
        }
        return img;
    }();
    return photo;
}

const ui::Bitmap& sample_photo(std::size_t index)
{
    constexpr int         Width  = 400;
    constexpr int         Height = 266;
    constexpr std::size_t Count  = 14;
    struct Palette
    {
        ui::Color sky_top, sky_horizon, sun, near_hill, far_hill, water;
    };
    static const std::array<Palette, 7>  palettes{{
        {ui::Color::from_rgba(40, 90, 170),
         ui::Color::from_rgba(170, 205, 235),
         ui::Color::from_rgba(255, 244, 210),
         ui::Color::from_rgba(40, 90, 50),
         ui::Color::from_rgba(110, 140, 150),
         ui::Color::from_rgba(60, 110, 160)},
        {ui::Color::from_rgba(70, 40, 110),
         ui::Color::from_rgba(250, 150, 90),
         ui::Color::from_rgba(255, 220, 140),
         ui::Color::from_rgba(50, 30, 50),
         ui::Color::from_rgba(120, 70, 90),
         ui::Color::from_rgba(110, 60, 90)},
        {ui::Color::from_rgba(20, 60, 90),
         ui::Color::from_rgba(120, 190, 200),
         ui::Color::from_rgba(250, 250, 230),
         ui::Color::from_rgba(30, 70, 60),
         ui::Color::from_rgba(80, 120, 120),
         ui::Color::from_rgba(30, 90, 110)},
        {ui::Color::from_rgba(90, 120, 160),
         ui::Color::from_rgba(220, 220, 210),
         ui::Color::from_rgba(255, 255, 240),
         ui::Color::from_rgba(90, 80, 60),
         ui::Color::from_rgba(150, 150, 150),
         ui::Color::from_rgba(100, 120, 140)},
        {ui::Color::from_rgba(10, 20, 60),
         ui::Color::from_rgba(60, 80, 140),
         ui::Color::from_rgba(235, 235, 255),
         ui::Color::from_rgba(15, 25, 40),
         ui::Color::from_rgba(40, 55, 90),
         ui::Color::from_rgba(20, 35, 70)},
        {ui::Color::from_rgba(120, 170, 220),
         ui::Color::from_rgba(240, 230, 200),
         ui::Color::from_rgba(255, 250, 220),
         ui::Color::from_rgba(150, 120, 60),
         ui::Color::from_rgba(190, 170, 130),
         ui::Color::from_rgba(90, 150, 170)},
        {ui::Color::from_rgba(60, 130, 120),
         ui::Color::from_rgba(200, 230, 190),
         ui::Color::from_rgba(255, 255, 230),
         ui::Color::from_rgba(30, 100, 40),
         ui::Color::from_rgba(100, 160, 100),
         ui::Color::from_rgba(50, 120, 110)},
    }};
    static std::array<ui::Bitmap, Count> photos;

    ui::Bitmap& photo = photos[index % Count];
    if (!photo.empty())
    {
        return photo;
    }
    const std::size_t seed      = index % Count;
    const Palette&    p         = palettes[seed % palettes.size()];
    const float       phase     = static_cast<float>(seed) * 1.7f;
    const float       horizon   = Height * (0.58f + 0.06f * static_cast<float>(seed % 3));
    const float       sun_x     = Width * (0.2f + 0.6f * static_cast<float>((seed * 37) % 11) / 10.0f);
    const float       sun_y     = horizon * (0.35f + 0.1f * static_cast<float>(seed % 4));
    const float       sun_r     = 16.0f + 4.0f * static_cast<float>(seed % 3);
    const bool        has_water = seed % 2 == 0;
    const auto        hill      = [&](float x, float base, float amplitude, float frequency)
    {
        return base -
               amplitude * (0.6f * std::sin(x * frequency + phase) + 0.3f * std::sin(x * frequency * 2.3f + phase * 1.9f) +
                            0.1f * std::sin(x * frequency * 5.1f + phase * 0.7f));
    };

    photo = ui::Bitmap(Width, Height);
    for (int y = 0; y < Height; ++y)
    {
        for (int x = 0; x < Width; ++x)
        {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            ui::Color   c  = ui::lerp(p.sky_top, p.sky_horizon, std::clamp(fy / horizon, 0.0f, 1.0f));
            const float dx = fx - sun_x;
            const float dy = fy - sun_y;
            const float d  = std::sqrt(dx * dx + dy * dy);
            if (d < sun_r * 2.2f)
            {
                c = ui::lerp(c,
                             p.sun,
                             std::clamp(1.0f - (d - sun_r) / (sun_r * 1.2f), 0.0f, 1.0f) * (d < sun_r ? 1.0f : 0.45f));
            }
            const float far  = hill(fx, horizon - 10.0f, 28.0f, 0.018f);
            const float mid  = hill(fx, horizon + 8.0f, 22.0f, 0.031f);
            const float near = hill(fx, horizon + 34.0f, 18.0f, 0.047f);
            if (fy > far)
            {
                c = ui::lerp(p.far_hill, p.sky_horizon, 0.25f);
            }
            if (fy > mid)
            {
                c = ui::lerp(p.far_hill, p.near_hill, 0.55f);
            }
            if (has_water && fy > horizon + 46.0f)
            {
                const float     mirrored = 2.0f * (horizon + 46.0f) - fy;
                const ui::Color sky    = ui::lerp(p.sky_top, p.sky_horizon, std::clamp(mirrored / horizon, 0.0f, 1.0f));
                const float     ripple = 0.5f + 0.5f * std::sin(fy * 0.9f + std::sin(fx * 0.05f) * 2.0f);
                c                      = ui::lerp(ui::lerp(sky, p.water, 0.55f), p.water, 0.2f * ripple);
            }
            else if (fy > near)
            {
                const float shade = std::clamp((fy - near) / 60.0f, 0.0f, 1.0f);
                const float grain = 0.04f * std::sin(fx * 0.9f) * std::sin(fy * 1.3f);
                c = ui::lerp(p.near_hill, ui::colors::Black, std::clamp(0.35f * shade + grain, 0.0f, 1.0f));
            }
            photo.set(x, y, ui::Color{c.r, c.g, c.b, 1.0f});
        }
    }
    return photo;
}

std::vector<CustomDataObject> data_objects(bool include_all_items)
{
    static const std::array<const char*, 8> DummyTexts{
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer id facilisis lectus. Cras nec convallis "
        "ante, quis pulvinar tellus. Integer dictum accumsan pulvinar. Pellentesque eget enim sodales sapien "
        "vestibulum consequat.",
        "Nullam eget mattis metus. Donec pharetra, tellus in mattis tincidunt, magna ipsum gravida nibh, vitae "
        "lobortis ante odio vel quam.",
        "Quisque accumsan pretium ligula in faucibus. Mauris sollicitudin augue vitae lorem cursus condimentum quis "
        "ac mauris. Pellentesque quis turpis non nunc pretium sagittis. Nulla facilisi. Maecenas eu lectus ante. "
        "Proin eleifend vel lectus non tincidunt. Fusce condimentum luctus nisi, in elementum ante tincidunt nec.",
        "Aenean in nisl at elit venenatis blandit ut vitae lectus. Praesent in sollicitudin nunc. Pellentesque justo "
        "augue, pretium at sem lacinia, scelerisque semper erat. Ut cursus tortor at metus lacinia dapibus.",
        "Ut consequat magna luctus justo egestas vehicula. Integer pharetra risus libero, et posuere justo mattis et.",
        "Proin malesuada, libero vitae aliquam venenatis, diam est faucibus felis, vitae efficitur erat nunc non "
        "mauris. Suspendisse at sodales erat.",
        "Aenean vulputate, turpis non tincidunt ornare, metus est sagittis erat, id lobortis orci odio eget quam. "
        "Suspendisse ex purus, lobortis quis suscipit a, volutpat vitae turpis.",
        "Duis facilisis, quam ut laoreet commodo, elit ex aliquet massa, non varius tellus lectus et nunc. Donec "
        "vitae risus ut ante pretium semper. Phasellus consectetur volutpat orci, eu dapibus turpis. Fusce varius "
        "sapien eu mattis pharetra.",
    };
    // The sample's Random, made repeatable.
    std::uint32_t seed   = 12345u;
    const auto    random = [&seed](int lo, int hi)
    {
        seed = seed * 1664525u + 1013904223u;
        return lo + static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(hi - lo));
    };
    std::vector<CustomDataObject> items;
    const int                     count = include_all_items ? 13 : 8;
    for (int i = 0; i < count; ++i)
    {
        const int views = random(100, 999);
        const int likes = random(10, 99);
        items.push_back(CustomDataObject{"Item " + std::to_string(i + 1),
                                         static_cast<std::size_t>(i),
                                         std::to_string(views),
                                         std::to_string(likes),
                                         DummyTexts[static_cast<std::size_t>(i) % DummyTexts.size()]});
    }
    return items;
}

const ui::AnimatedIconSource& pulse_visual()
{
    static const PulseVisual visual{ui::Color::from_rgba(0, 120, 215)};
    return visual;
}

// ---- search ----

namespace
{
// ASCII only: every page name is ASCII, and a UTF-8 byte above 0x7F is left alone.
std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::vector<std::string> query_words(std::string_view query)
{
    std::vector<std::string> words;
    const std::string        lower = lowered(query);
    std::size_t              start = 0;
    while (start < lower.size())
    {
        const std::size_t end = std::min(lower.find(' ', start), lower.size());
        if (end > start)
        {
            words.push_back(lower.substr(start, end - start));
        }
        start = end + 1;
    }
    return words;
}

ui::Color secondary_text(const ui::Theme& t) // the secondary text color
{
    return t.colors().text_secondary;
}

// A search result: the page's name over its description, a click going to the page.
ui::Spec result_card(const SearchHit& hit, Context& ctx)
{
    constexpr float CardWidth = 360.0f;
    constexpr float Padding   = 16.0f;
    return ui::Button{
        .padding       = ui::Symmetric{.horizontal = Padding, .vertical = 12.0f},
        .width         = CardWidth,
        .min_height    = 90.0f,
        .accessibility = {.name = hit.page->name},
        .on_click      = [&ctx, name = std::string{hit.page->name}] { ctx.navigate(name); },
        .content =
            ui::VerticalStack{
                .spacing         = 4.0f,
                .child_alignment = ui::Alignment::Stretch,
                .width           = CardWidth - 2.0f * Padding,
                .content =
                    {
                        ui::Text{.text     = hit.page->name,
                                 .role     = ui::TextRole::BodyStrong,
                                 .trimming = ui::TextTrimming::CharacterEllipsis},
                        ui::Text{.text       = hit.page->description,
                                 .role       = ui::TextRole::Caption,
                                 .wrapping   = ui::TextWrapping::WrapWholeWords,
                                 .foreground = secondary_text},
                    },
            },
    };
}

struct SearchResultsState
{
    ui::Property<std::size_t> filter{0};
};
} // namespace

std::vector<SearchHit> search_pages(std::string_view query)
{
    std::vector<SearchHit>         hits;
    const std::vector<std::string> words = query_words(query);
    if (words.empty())
    {
        return hits;
    }
    for (const Group& group : groups())
    {
        const std::string group_title = lowered(group.title);
        for (const Page& page : group.pages)
        {
            const std::string name        = lowered(page.name);
            const std::string description = lowered(page.description);
            const bool        all_found   = std::ranges::all_of(words,
                                                                [&](const std::string& word)
                                                                {
                                                           return name.contains(word) || description.contains(word) ||
                                                                  group_title.contains(word);
                                                                });
            if (all_found)
            {
                hits.push_back({&page, &group});
            }
        }
    }
    return hits;
}

std::vector<SearchHit> search_suggestions(std::string_view query)
{
    std::vector<SearchHit> hits   = search_pages(query);
    const std::string      prefix = lowered(query);
    std::ranges::stable_sort(hits,
                             [&](const SearchHit& a, const SearchHit& b)
                             {
                                 const bool a_starts = lowered(a.page->name).starts_with(prefix);
                                 const bool b_starts = lowered(b.page->name).starts_with(prefix);
                                 if (a_starts != b_starts)
                                 {
                                     return a_starts;
                                 }
                                 return std::string_view{a.page->name} < std::string_view{b.page->name};
                             });
    return hits;
}

ui::Spec search_results_page(std::string query, Context& ctx)
{
    const std::vector<SearchHit> hits   = search_pages(query);
    const std::string            header = "Results for \"" + query + "\"";
    if (hits.empty())
    {
        return ui::VerticalStack{
            .spacing         = 10.0f,
            .child_alignment = ui::Alignment::Stretch,
            .margin          = ui::Edges{.left = 28.0f, .top = 22.0f, .right = 28.0f, .bottom = 28.0f},
            .content =
                {
                    ui::Text{.text = header, .role = ui::TextRole::Title, .horizontal_alignment = ui::Alignment::Leading},
                    ui::Text{.text = "No results match your search.", .horizontal_alignment = ui::Alignment::Leading},
                },
        };
    }

    // The filters: All, then each group with a hit, in navigation order.
    std::vector<ui::SelectorBarItem>    filter_names{"All (" + std::to_string(hits.size()) + ")"};
    std::vector<std::vector<SearchHit>> filtered{hits};
    for (const Group& group : groups())
    {
        std::vector<SearchHit> in_group;
        std::ranges::copy_if(hits,
                             std::back_inserter(in_group),
                             [&](const SearchHit& hit) { return hit.group == &group; });
        if (!in_group.empty())
        {
            filter_names.push_back(std::string{group.title} + " (" + std::to_string(in_group.size()) + ")");
            filtered.push_back(std::move(in_group));
        }
    }

    return with_state<SearchResultsState>(
        [&](SearchResultsState& s) -> ui::Spec
        {
            return ui::VerticalStack{
                .spacing         = 10.0f,
                .child_alignment = ui::Alignment::Stretch,
                .margin          = ui::Edges{.left = 28.0f, .top = 22.0f, .right = 28.0f, .bottom = 28.0f},
                .content =
                    {
                        ui::Text{.text = header, .role = ui::TextRole::Title, .horizontal_alignment = ui::Alignment::Leading},
                        ui::SelectorBar{.items                = std::move(filter_names),
                                        .selected_index       = s.filter,
                                        .horizontal_alignment = ui::Alignment::Leading,
                                        .accessibility        = {.name = "Search result filters"}},
                        ui::Match<std::size_t>{
                            .value   = s.filter,
                            .content = [&ctx, filtered = std::move(filtered)](const std::size_t& index) -> ui::Spec
                            {
                                if (index >= filtered.size())
                                {
                                    return {};
                                }
                                ui::SpecList cards;
                                for (const SearchHit& hit : filtered[index])
                                {
                                    cards.nodes.push_back(result_card(hit, ctx).take());
                                }
                                return ui::WrapPanel{.item_spacing = 12.0f,
                                                     .line_spacing = 12.0f,
                                                     .margin       = ui::Edges{.top = 12.0f},
                                                     .content      = std::move(cards)};
                            },
                        },
                    },
            };
        });
}

// ---- icons ---------------------------------------------------------------------------------------

namespace
{

// Each page's icon and each group's, by name, in name order.
constexpr std::array<std::pair<std::string_view, ui::Symbol>, 111> PageIcons{{
    {"Acrylic", ui::Symbol::Preview},
    {"AnimatedIcon", ui::Symbol::Emoji},
    {"AnimatedVisualPlayer", ui::Symbol::Play},
    {"Animation Interop", ui::Symbol::Rotate},
    {"AnnotatedScrollBar", ui::Symbol::Bookmarks},
    {"AppBarButton", ui::Symbol::Add},
    {"AppBarSeparator", ui::Symbol::Remove},
    {"AppBarToggleButton", ui::Symbol::Pin},
    {"AutoSuggestBox", ui::Symbol::Find},
    {"Backdrops", ui::Symbol::SetLockScreen},
    {"Border", ui::Symbol::Stop},
    {"Breadcrumb", ui::Symbol::Forward},
    {"Button", ui::Symbol::TouchPointer},
    {"CalendarDatePicker", ui::Symbol::CalendarDay},
    {"CalendarView", ui::Symbol::Calendar},
    {"Canvas", ui::Symbol::MapPin},
    {"Carousel", ui::Symbol::SlideShow},
    {"CheckBox", ui::Symbol::Accept},
    {"Clipboard", ui::Symbol::Paste},
    {"ColorPicker", ui::Symbol::FontColor},
    {"ComboBox", ui::Symbol::Filter},
    {"Command", ui::Symbol::Repair},
    {"CommandBar", ui::Symbol::More},
    {"CommandBarFlyout", ui::Symbol::Manage},
    {"Connected Animation", ui::Symbol::Link},
    {"DatePicker", ui::Symbol::CalendarWeek},
    {"DensityScope", ui::Symbol::ZoomOut},
    {"Dialog", ui::Symbol::Caption},
    {"Drag and Drop", ui::Symbol::MoveToFolder},
    {"DropdownButton", ui::Symbol::OpenWith},
    {"Easing Functions", ui::Symbol::Go},
    {"EditBox", ui::Symbol::Keyboard},
    {"Element Transitions", ui::Symbol::Orientation},
    {"Expander", ui::Symbol::ShowResults},
    {"Flyout", ui::Symbol::Memo},
    {"ForEach", ui::Symbol::RepeatAll},
    {"Grid", ui::Symbol::ViewAll},
    {"GroupBox", ui::Symbol::ContactInfo},
    {"Icon", ui::Symbol::Emoji2},
    {"Image", ui::Symbol::Pictures},
    {"Implicit Transitions", ui::Symbol::Sync},
    {"InfoBadge", ui::Symbol::Flag},
    {"InfoBar", ui::Symbol::Important},
    {"InkCanvas", ui::Symbol::Edit},
    {"Item Layouts", ui::Symbol::Sort},
    {"LayoutPanel", ui::Symbol::DockLeft},
    {"Line", ui::Symbol::Trim},
    {"ListView", ui::Symbol::List},
    {"Match", ui::Symbol::Priority},
    {"MenuBar", ui::Symbol::Bullets},
    {"MenuFlyout", ui::Symbol::Setting},
    {"Message Box", ui::Symbol::Message},
    {"Native", ui::Symbol::Remote},
    {"NavigationShell", ui::Symbol::GlobalNavigationButton},
    {"Notifications", ui::Symbol::Mail},
    {"NumberBox", ui::Symbol::Calculator},
    {"Page Transitions", ui::Symbol::Page2},
    {"Pager", ui::Symbol::Page},
    {"Parallax", ui::Symbol::Map},
    {"PasswordBox", ui::Symbol::Permissions},
    {"PersonPicture", ui::Symbol::Contact},
    {"PipsPager", ui::Symbol::Next},
    {"Popup", ui::Symbol::PreviewLink},
    {"Printing", ui::Symbol::Print},
    {"ProgressBar", ui::Symbol::Download},
    {"ProgressRing", ui::Symbol::Clock},
    {"PullToRefresh", ui::Symbol::Refresh},
    {"RadialGradientBrush", ui::Symbol::Highlight},
    {"RadioButton", ui::Symbol::Target},
    {"Rating", ui::Symbol::SolidStar},
    {"RelativePanel", ui::Symbol::Directions},
    {"ResizeGripper", ui::Symbol::Crop},
    {"RichEdit", ui::Symbol::Underline},
    {"RichEditBox", ui::Symbol::Font},
    {"RichText", ui::Symbol::Read},
    {"ScaleToFit", ui::Symbol::ZoomIn},
    {"Scroll", ui::Symbol::Up},
    {"Sections", ui::Symbol::Document},
    {"SelectorBar", ui::Symbol::Tag},
    {"Shadow", ui::Symbol::Copy},
    {"Shape", ui::Symbol::OutlineStar},
    {"SidePane", ui::Symbol::OpenPane},
    {"Single Instance", ui::Symbol::Admin},
    {"Slider", ui::Symbol::Volume},
    {"SplitButton", ui::Symbol::Send},
    {"Splitter", ui::Symbol::DockRight},
    {"Stack", ui::Symbol::AlignLeft},
    {"StandardCommand", ui::Symbol::Cut},
    {"Storage Pickers", ui::Symbol::OpenFile},
    {"Swipe", ui::Symbol::Delete},
    {"TableView", ui::Symbol::Calendar},
    {"Tabs", ui::Symbol::Library},
    {"TeachingTip", ui::Symbol::Help},
    {"Text", ui::Symbol::Character},
    {"TextBox", ui::Symbol::Rename},
    {"TilePanel", ui::Symbol::SetTile},
    {"TileView", ui::Symbol::BrowsePhotos},
    {"TimePicker", ui::Symbol::Clock},
    {"TitleBar", ui::Symbol::Home},
    {"ToggleButton", ui::Symbol::Bold},
    {"ToggleSplitButton", ui::Symbol::Italic},
    {"ToggleSwitch", ui::Symbol::Switch},
    {"ToolTip", ui::Symbol::Comment},
    {"Toolbar", ui::Symbol::AllApps},
    {"TreeView", ui::Symbol::Folder},
    {"TwoPane", ui::Symbol::TwoPage},
    {"Window", ui::Symbol::NewWindow},
    {"Window States", ui::Symbol::BackToWindow},
    {"Window Title Bar", ui::Symbol::FullScreen},
    {"Windowing", ui::Symbol::SwitchApps},
    {"WrapPanel", ui::Symbol::Shuffle},
}};

constexpr std::array<std::pair<std::string_view, ui::Symbol>, 16> GroupIcons{{
    {"Basic Input", ui::Symbol::TouchPointer},
    {"Collections", ui::Symbol::ViewAll},
    {"Date & Time", ui::Symbol::Calendar},
    {"Dialogs & Flyouts", ui::Symbol::Comment},
    {"Layout", ui::Symbol::DockLeft},
    {"Media", ui::Symbol::Pictures},
    {"Menus & Toolbars", ui::Symbol::More},
    {"Motion", ui::Symbol::Play},
    {"Native Controls", ui::Symbol::Remote},
    {"Navigation", ui::Symbol::GlobalNavigationButton},
    {"Scrolling", ui::Symbol::Up},
    {"Status & Info", ui::Symbol::Important},
    {"Styles", ui::Symbol::FontColor},
    {"System", ui::Symbol::Setting},
    {"Text", ui::Symbol::Font},
    {"Windowing", ui::Symbol::NewWindow},
}};

static_assert(std::ranges::is_sorted(PageIcons, {}, &std::pair<std::string_view, ui::Symbol>::first));
static_assert(std::ranges::is_sorted(GroupIcons, {}, &std::pair<std::string_view, ui::Symbol>::first));

template <std::size_t N>
ui::IconSource icon_in(const std::array<std::pair<std::string_view, ui::Symbol>, N>& table, std::string_view name)
{
    const auto it = std::ranges::lower_bound(table, name, {}, &std::pair<std::string_view, ui::Symbol>::first);
    return it != table.end() && it->first == name ? ui::IconSource{it->second} : ui::IconSource{};
}

} // namespace

ui::IconSource page_icon(std::string_view page)
{
    return icon_in(PageIcons, page);
}

ui::IconSource group_icon(std::string_view group)
{
    return icon_in(GroupIcons, group);
}

std::span<const Group> groups()
{
    // The groups in navigation order. A group libwinui has no control for yet is left out.
    static const std::array<Group, 16> Groups{{
        {"Menus & Toolbars", menus_and_toolbars_pages()},
        {"Collections", collections_pages()},
        {"Basic Input", basic_input_pages()},
        {"Date & Time", date_and_time_pages()},
        {"Status & Info", status_and_info_pages()},
        {"Dialogs & Flyouts", dialogs_and_flyouts_pages()},
        {"Scrolling", scrolling_pages()},
        {"Layout", layout_pages()},
        {"Navigation", navigation_pages()},
        {"Media", media_pages()},
        {"Styles", styles_pages()},
        {"Text", text_pages()},
        {"Motion", motion_pages()},
        {"Windowing", windowing_pages()},
        // libwinui's own group: the native HWND hosts demonstrate the escape hatch.
        {"Native Controls", native_pages()},
        {"System", system_pages()},
    }};
    return Groups;
}

} // namespace gallery

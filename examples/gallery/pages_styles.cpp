// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <array>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <cmath>
#include <cstddef>


using ui::operator""_fr;

namespace gallery
{
namespace
{
// The named colors the samples paint with.
constexpr ui::Color Aqua          = ui::Color::from_argb(0xFF00FFFF);
constexpr ui::Color Magenta       = ui::Color::from_argb(0xFFFF00FF);
constexpr ui::Color Yellow        = ui::Color::from_argb(0xFFFFFF00);
constexpr ui::Color Blue          = ui::Color::from_argb(0xFF0000FF);
constexpr ui::Color Red           = ui::Color::from_argb(0xFFFF0000);
constexpr ui::Color Green         = ui::Color::from_argb(0xFF008000);
constexpr ui::Color SkyBlue       = ui::Color::from_argb(0xFF87CEEB);
constexpr ui::Color SteelBlue     = ui::Color::from_argb(0xFF4682B4);
constexpr ui::Color DarkGoldenrod = ui::Color::from_argb(0xFFB8860B);
constexpr ui::Color Lavender      = ui::Color::from_argb(0xFFCCCCFF); // the geometry group sample's #CCCCFF

ui::Spec options_panel(float width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// A Slider with a header driving `value`, with its min, max and step_frequency.
// `tick` is the tick_frequency; 0 draws no tick marks.
ui::Spec slider(const char* header, float min, float max, ui::Property<float>& value, float step = 0.5f, float tick = 0.0f)
{
    return ui::Slider{.header = header, .value = value, .min = min, .max = max, .tick_frequency = tick, .step_frequency = step};
}

// A paragraph: the body style, wrapping.
ui::Spec paragraph(const char* text, ui::Thickness margin = ui::Edges{.bottom = 12.0f})
{
    return ui::Text{.text = text, .wrapping = ui::TextWrapping::WrapWholeWords, .margin = margin};
}

// ---- stand-ins for the samples' assets ----

// Stands in for the sample's Slices image. The real slices touch, so its monochrome form is a
// plain disc; the gaps here keep the pie readable in monochrome.
const ui::Bitmap& slices_image()
{
    static const ui::Bitmap image = []
    {
        constexpr int                      Size = 64;
        constexpr std::array<ui::Color, 6> Colors{ui::Color::from_argb(0xFFE81123),
                                                  ui::Color::from_argb(0xFFFF8C00),
                                                  ui::Color::from_argb(0xFFFFB900),
                                                  ui::Color::from_argb(0xFF107C10),
                                                  ui::Color::from_argb(0xFF0078D4),
                                                  ui::Color::from_argb(0xFF881798)};
        ui::Bitmap                         img(Size, Size, ui::colors::Transparent);
        const float                        c = (Size - 1) * 0.5f;
        for (int y = 0; y < Size; ++y)
        {
            for (int x = 0; x < Size; ++x)
            {
                const float dx = static_cast<float>(x) - c;
                const float dy = static_cast<float>(y) - c;
                if (dx * dx + dy * dy > c * c)
                {
                    continue;
                }
                float       angle = std::atan2(dy, dx) + std::numbers::pi_v<float>;
                const float turns = angle / (2.0f * std::numbers::pi_v<float>)*Colors.size();
                // A 3px gap along each boundary between two slices.
                const float boundary = std::abs(turns - std::round(turns)) * 2.0f * std::numbers::pi_v<float> /
                                       Colors.size() * std::sqrt(dx * dx + dy * dy);
                if (boundary < 1.5f)
                {
                    continue;
                }
                const auto slice = static_cast<std::size_t>(turns) % Colors.size();
                img.set(x, y, Colors[slice]);
            }
        }
        return img;
    }();
    return image;
}

// The LibreICONS "camera panorama" SVG the sample loads from GitHub: a color camera over a
// panorama strip, drawn here as a 48x48 document of the same kind.
const ui::SvgImage& camera_svg()
{
    static const ui::SvgImage svg{R"(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48">
<rect x="3" y="12" width="42" height="28" rx="4" fill="#3A96DD"/>
<rect x="16" y="7" width="16" height="7" rx="2" fill="#2B7BB9"/>
<path d="M7 34 L17 23 L24 30 L30 25 L41 34 Z" fill="#9BD6F2"/>
<circle cx="24" cy="26" r="9" fill="#FFFFFF"/>
<circle cx="24" cy="26" r="6" fill="#1F4E79"/>
<circle cx="36" cy="18" r="2" fill="#FFB900"/>
</svg>)"};
    return svg;
}

// ---- Acrylic ----

// The samples' backdrop: an aqua bar down the left, a magenta disc in the middle and a yellow
// block in the bottom-right corner, all in one cell for the acrylic above them to blur.
ui::Spec acrylic_backdrop()
{
    return ui::Grid{
        .columns = {1_fr},
        .rows    = {1_fr},
        .content =
            {
                ui::Cell{.content = ui::Rectangle{.fill                 = Aqua,
                                                  .width                = 100.0f,
                                                  .height               = 200.0f,
                                                  .horizontal_alignment = ui::Alignment::Leading,
                                                  .vertical_alignment   = ui::Alignment::Leading}},
                ui::Cell{.content = ui::Ellipse{.fill                 = Magenta,
                                                .width                = 152.0f,
                                                .height               = 152.0f,
                                                .horizontal_alignment = ui::Alignment::Center,
                                                .vertical_alignment   = ui::Alignment::Center}},
                ui::Cell{.content = ui::Rectangle{.fill                 = Yellow,
                                                  .width                = 80.0f,
                                                  .height               = 100.0f,
                                                  .horizontal_alignment = ui::Alignment::Trailing,
                                                  .vertical_alignment   = ui::Alignment::Trailing}},
            },
    };
}

// Filled with the theme's default in-app acrylic. A ui::RectangleControl holds a plain
// brush, so this re-reads the theme token every frame to follow a theme switch.
class ThemedAcrylic final : public ui::Element
{
public:

protected:
    ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{};
    }
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        ui::DrawGeometry cmd;
        cmd.geometry = ui::PathGeometry::rectangle(bounds());
        cmd.fill = has_fluent_palette(theme) ? theme.palette.acrylic_in_app_default() : ui::Brush{theme.surfaces.menu};
        cmd.brush_box = bounds();
        renderer.draw_geometry(cmd);
    }
};

struct AcrylicState
{
    // Custom acrylic in-app brush: tint_color, tint_opacity and fallback_color from the options.
    ui::Property<float>                      custom_opacity{0.8f};
    ui::Property<std::optional<std::size_t>> custom_tint{0};
    ui::Property<std::optional<std::size_t>> custom_fallback{0};
    ui::Property<ui::Paint>                  custom{};
    // Luminosity with in-app Acrylic: SkyBlue, tint_opacity and tint_luminosity_opacity.
    ui::Property<float>     lumin_opacity{0.8f};
    ui::Property<float>     luminosity{0.8f};
    ui::Property<ui::Paint> lumin{};

    void update()
    {
        constexpr std::array<ui::Color, 3> Tints{ui::colors::Black, Red, Blue};
        constexpr std::array<ui::Color, 2> Fallbacks{Green, Yellow};
        ui::AcrylicBrush                   brush;
        brush.tint_color     = Tints[custom_tint.get().value_or(0) % Tints.size()];
        brush.tint_opacity   = custom_opacity.get();
        brush.fallback_color = Fallbacks[custom_fallback.get().value_or(0) % Fallbacks.size()];
        custom               = brush;

        ui::AcrylicBrush sky;
        sky.tint_color              = SkyBlue;
        sky.fallback_color          = SkyBlue;
        sky.tint_opacity            = lumin_opacity.get();
        sky.tint_luminosity_opacity = luminosity.get();
        lumin                       = sky;
    }
};

// The two option cards' sample: the acrylic rectangle (a 12 margin) over the backdrop, as large as
// the default brush's sample so the page's three samples line up.
ui::Spec acrylic_pair(ui::Property<ui::Paint>& fill)
{
    return ui::Grid{
        .columns = {1_fr},
        .rows    = {1_fr},
        .width   = 400.0f,
        .height  = 252.0f,
        .content =
            {
                ui::Cell{.content = acrylic_backdrop()},
                ui::Cell{.content = ui::Rectangle{.fill = fill, .margin = ui::Thickness{12.0f}}},
            },
    };
}

ui::Spec acrylic_page(Context&)
{
    return with_state<AcrylicState>(
        [](AcrylicState& s)
        {
            s.update();
            return examples({
                paragraph("An AcrylicBrush falls back to its solid fallback color where it cannot be drawn: under a "
                          "theme without acrylic, or on a renderer that cannot blur. AcrylicBrush is in-app acrylic, "
                          "which blurs the app's own content behind it; see Backdrops for acrylic behind the "
                          "whole window.",
                          ui::Thickness{}),
                example({
                    .title    = "Default in-app acrylic brush.",
                    .code     = R"cpp(
                        // The theme's in-app acrylic: it blurs the app's own content behind it.
                        const ui::Brush acrylic = ui::Theme::light().palette.acrylic_in_app_default();

                        ui::Grid{
                            .columns = {1_fr},
                            .rows    = {1_fr},
                            .width   = 400,
                            .height  = 252,
                            .content   = {
                                // What the acrylic blurs.
                                ui::Cell{.content = ui::Ellipse{
                                     .fill                 = ui::Color::from_argb(0xFFFF00FF),
                                     .width                = 152,
                                     .height               = 152,
                                     .horizontal_alignment = ui::Alignment::Center,
                                     .vertical_alignment   = ui::Alignment::Center,
                                 }},
                                // The acrylic on top, in the same cell.
                                ui::Cell{.content = ui::Rectangle{.fill = acrylic, .margin = ui::Thickness{12.0f}}},
                            },
                        }
                    )cpp",
                    .controls = {ui::Grid{
                        .columns = {1_fr},
                        .rows    = {1_fr},
                        .width   = 400.0f,
                        .height  = 252.0f,
                        .content =
                            {
                                ui::Cell{.content = acrylic_backdrop()},
                                ui::Cell{.content = []
                                 {
                                     std::unique_ptr<ui::Element> panel = std::make_unique<ThemedAcrylic>();
                                     panel->set_margin(ui::Thickness{12.0f});
                                     return panel;
                                 }()},
                            },
                    }},
                }),
                example({
                    .title   = "Custom acrylic in-app brush.",
                    .code    = R"cpp(
                        ui::Property<float>                    tint_opacity{0.8f};
                        ui::Property<ui::Paint> fill{};

                        auto update = [&]
                        {
                            ui::AcrylicBrush brush;
                            brush.tint_color     = ui::colors::Black;
                            brush.tint_opacity   = tint_opacity.get();
                            brush.fallback_color = ui::Color::from_argb(0xFF008000); // where it cannot blur
                            fill                 = brush;
                        };
                        update();

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Slider{
                                    .value            = tint_opacity,
                                    .min              = 0.0f,
                                    .max              = 1.0f,
                                    .step_frequency   = 0.001f,
                                    .on_value_changed = [&](double) { update(); },
                                },
                                // Laid over the content it blurs.
                                ui::Rectangle{.fill = fill, .margin = ui::Thickness{12.0f}},
                            },
                        }
                    )cpp",
                    .options = options_panel(200.0f,
                                             {
                                                 ui::Text{.text = "Tint Opacity :"},
                                                 ui::Slider{.value            = s.custom_opacity,
                                                            .min              = 0.0f,
                                                            .max              = 1.0f,
                                                            .step             = 0.001f,
                                                            .step_frequency   = 0.001f,
                                                            .accessibility    = {.name = "tint opacity"},
                                                            .on_value_changed =
                                                                [&s](double v)
                                                            {
                                                                s.custom_opacity = static_cast<float>(static_cast<float>(v));
                                                                s.update();
                                                            }},
                                                 ui::Text{.text = "Tint Color :"},
                                                 ui::ComboBox{.items                = {"Black", "Red", "Blue"},
                                                              .selected_index       = s.custom_tint,
                                                              .accessibility        = {.name = "tint color"},
                                                              .on_selection_changed =
                                                                  [&s](std::optional<std::size_t> i)
                                                              {
                                                                  s.custom_tint = i.value_or(0);
                                                                  s.update();
                                                              }},
                                                 ui::Text{.text = "Fallback Color :"},
                                                 ui::ComboBox{.items                = {"Green", "Yellow"},
                                                              .selected_index       = s.custom_fallback,
                                                              .accessibility        = {.name = "fallback color"},
                                                              .on_selection_changed =
                                                                  [&s](std::optional<std::size_t> i)
                                                              {
                                                                  s.custom_fallback = i.value_or(0);
                                                                  s.update();
                                                              }},
                                             }),
                    .controls = {acrylic_pair(s.custom)},
                }),
                example({
                    .title   = "Luminosity with in-app Acrylic.",
                    .code    = R"cpp(
                        ui::Property<float>                    tint_opacity{0.8f};
                        ui::Property<float>                    luminosity{0.8f};
                        ui::Property<ui::Paint> fill{};

                        auto update = [&]
                        {
                            const ui::Color sky_blue = ui::Color::from_argb(0xFF87CEEB);
                            ui::AcrylicBrush brush;
                            brush.tint_color              = sky_blue;
                            brush.fallback_color          = sky_blue;
                            brush.tint_opacity            = tint_opacity.get();
                            brush.tint_luminosity_opacity = luminosity.get();
                            fill                          = brush;
                        };
                        update();

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Slider{
                                    .value            = tint_opacity,
                                    .min              = 0.0f,
                                    .max              = 1.0f,
                                    .step_frequency   = 0.001f,
                                    .on_value_changed = [&](double) { update(); },
                                },
                                ui::Slider{
                                    .value            = luminosity,
                                    .min              = 0.0f,
                                    .max              = 1.0f,
                                    .step_frequency   = 0.001f,
                                    .on_value_changed = [&](double) { update(); },
                                },
                                // Laid over the content it blurs.
                                ui::Rectangle{.fill = fill, .margin = ui::Thickness{12.0f}},
                            },
                        }
                    )cpp",
                    .options = options_panel(200.0f,
                                             {
                                                 ui::Text{.text = "Tint Opacity :"},
                                                 ui::Slider{.value            = s.lumin_opacity,
                                                            .min              = 0.0f,
                                                            .max              = 1.0f,
                                                            .step             = 0.001f,
                                                            .step_frequency   = 0.001f,
                                                            .accessibility    = {.name = "tint opacity"},
                                                            .on_value_changed =
                                                                [&s](double v)
                                                            {
                                                                s.lumin_opacity = static_cast<float>(static_cast<float>(v));
                                                                s.update();
                                                            }},
                                                 ui::Text{.text = "Tint Luminosity Opacity :"},
                                                 ui::Slider{.value            = s.luminosity,
                                                            .min              = 0.0f,
                                                            .max              = 1.0f,
                                                            .step             = 0.001f,
                                                            .step_frequency   = 0.001f,
                                                            .accessibility    = {.name = "tint luminosity"},
                                                            .on_value_changed =
                                                                [&s](double v)
                                                            {
                                                                s.luminosity = static_cast<float>(static_cast<float>(v));
                                                                s.update();
                                                            }},
                                             }),
                    .controls = {acrylic_pair(s.lumin)},
                }),
            });
        });
}

// ---- AnimatedIcon ----

// The Kind options, in the sample's order, and the sources they name.
struct AnimatedKinds
{
    ui::AnimatedBackVisualSource                   back;
    ui::AnimatedChevronDownSmallVisualSource       chevron_down;
    ui::AnimatedChevronRightDownSmallVisualSource  chevron_right_down;
    ui::AnimatedChevronUpDownSmallVisualSource     chevron_up_down;
    ui::AnimatedFindVisualSource                   find;
    ui::AnimatedGlobalNavigationButtonVisualSource global_navigation;
    ui::AnimatedSettingsVisualSource               settings;

    [[nodiscard]] const ui::AnimatedIconSource* at(std::size_t i) const
    {
        const std::array<const ui::AnimatedIconSource*, 7>
            all{&back, &chevron_down, &chevron_right_down, &chevron_up_down, &find, &global_navigation, &settings};
        return all[i % all.size()];
    }
};

const AnimatedKinds& animated_kinds()
{
    static const AnimatedKinds kinds;
    return kinds;
}

struct AnimatedIconState
{
    ui::Property<std::optional<std::size_t>> kind{4}; // AnimatedFindVisualSource
    ui::Control<ui::AnimatedIcon>            icon;
};

ui::Spec animated_icon_page(Context&)
{
    return with_state<AnimatedIconState>(
        [](AnimatedIconState& s)
        {
            return examples(
                {
                    example(
                        {
                            .title = "Adding AnimatedIcon to a button",
                            .code  = R"cpp(
                        // The source outlives the icon that plays it.
                        static const ui::AnimatedFindVisualSource find;
                        ui::Control<ui::AnimatedIcon>             icon;

                        ui::Button{
                            .width              = 75,
                            .on_pointer_entered = [&] { icon->set_state("PointerOver"); },
                            .on_pointer_exited  = [&] { icon->set_state("Normal"); },
                            .content            = ui::AnimatedIcon{
                                .source               = &find,
                                .fallback = ui::symbol_glyph(ui::Symbol::Find),
                                .state                = std::string{"Normal"},
                                .control              = icon,
                            },
                        }

                        // Another stock animation, later:
                        static const ui::AnimatedSettingsVisualSource settings;
                        icon->set_source(&settings);
                    )cpp",
                            .options =
                                ui::ComboBox{
                                    .header             = "Kind",
                                    .items              = {"AnimatedBackVisualSource",
                                                           "AnimatedChevronDownSmallVisualSource",
                                                           "AnimatedChevronRightDownSmallVisualSource",
                                                           "AnimatedChevronUpDownSmallVisualSource",
                                                           "AnimatedFindVisualSource",
                                                           "AnimatedGlobalNavigationButtonVisualSource",
                                                           "AnimatedSettingsVisualSource"},
                                    .selected_index     = s.kind,
                                    .min_width          = 340.0f,
                                    .vertical_alignment = ui::Alignment::Center,
                                },
                            .controls =
                                {
                                    ui::VerticalStack{
                                        .child_alignment = ui::Alignment::Leading,
                                        .content =
                                            {
                                                paragraph("The following example is a button that the user clicks to "
                                                          "load a search "
                                                          "experience. The AnimatedIcon plays one of the stock "
                                                          "animated visuals, "
                                                          "their shapes, key frames and markers drawn by libwinui's "
                                                          "own renderer; Kind "
                                                          "picks which. The button moves the icon to its PointerOver "
                                                          "state when the "
                                                          "pointer enters it and back to Normal when it leaves."),
                                                ui::Button{.width         = 75.0f,
                                                           .accessibility = {.name = "AnimatedIcon Example"},
                                                           .on_pointer_entered =
                                                               [&s]
                                                           {
                                                               if (s.icon != nullptr)
                                                               {
                                                                   s.icon->set_state("PointerOver");
                                                               }
                                                           },
                                                           .on_pointer_exited =
                                                               [&s]
                                                           {
                                                               if (s.icon != nullptr)
                                                               {
                                                                   s.icon->set_state("Normal");
                                                               }
                                                           },
                                                           .content = ui::AnimatedIcon{.source = ui::bind(s.kind,
                                                                                                          [](std::optional<std::size_t> i) {
                                                                                                              return animated_kinds()
                                                                                                                  .at(i.value_or(
                                                                                                                      0));
                                                                                                          }),
                                                                                       .fallback = ui::symbol_glyph(ui::Symbol::Find),
                                                                                       // The icon starts in Normal, so the first
                                                                                       // hover plays the Normal to PointerOver segment.
                                                                                       .state   = std::string{"Normal"},
                                                                                       .control = s
                                                                                                      .icon}},
                                            },
                                    }},
                        }),
                    example({
                        .title   = "Adding AnimatedIcon to a NavigationShell",
                        .code    = R"cpp(
                        // The source outlives the view; the item drives the icon's states itself.
                        static const ui::AnimatedSettingsVisualSource settings;

                        ui::NavigationShell{
                            .settings_visible    = false,
                            .items               = {ui::NavItem{
                                .text         = "Game Settings",
                                .icon          = ui::IconGlyph{0xE713}, // the fallback
                                .animated_icon = &settings,
                            }},
                        }
                    )cpp",
                        .stretch = true,
                        .controls =
                            {
                                ui::VerticalStack{
                                    .content =
                                        {
                                            paragraph("If you set an AnimatedIcon as the icon of a NavigationShell "
                                                      "item, the item "
                                                      "sets the states of the AnimatedIcon for you, according to the "
                                                      "states of the "
                                                      "control.\n\nFor this example, the item's icon is the stock "
                                                      "settings "
                                                      "animation, AnimatedSettingsVisualSource, with the settings "
                                                      "glyph as its "
                                                      "fallback."),
                                            ui::NavigationShell{.settings_visible = false,
                                                                .items = {ui::NavItem{.text = "Game Settings",
                                                                                      .icon  = ui::IconGlyph{0xE713},
                                                                                      .animated_icon = &animated_kinds().settings}}},
                                        },
                                }},
                    }),
                });
        });
}

// ---- DensityScope ----

struct CompactState
{
    ui::Property<std::optional<std::size_t>> density{0}; // 0 Standard, 1 Compact
};

ui::Spec density_scope_page(Context&)
{
    return with_state<CompactState>(
        [](CompactState& s)
        {
            ui::TextStyle heading = ui::Theme::light().text_style;
            heading.font_size     = 18.0f;
            return examples({
                ui::RichText{.blocks =
                                 {
                                     {.inlines = {{.text   = "Controls that support compact styling:",
                                                   .weight = ui::FontWeight::Bold}},
                                      .margin  = ui::Edges{.bottom = 4.0f}},
                                     {.inlines = {{.text = "• TextBox"}}, .margin = ui::Edges{.bottom = 4.0f}},
                                     {.inlines = {{.text = "• AutoSuggestBox"}}, .margin = ui::Edges{.bottom = 4.0f}},
                                     {.inlines = {{.text = "• NumberBox"}}, .margin = ui::Edges{.bottom = 4.0f}},
                                     {.inlines = {{.text = "• ComboBox"}}},
                                 },
                             .margin = ui::Edges{.top = 24.0f}},
                example({
                    .title    = "Compact Sizing for controls",
                    .code     = R"cpp(
                            ui::Property<std::optional<std::size_t>> density{0}; // 0 Standard, 1 Compact

                            ui::VerticalStack{
                                .spacing = 12,
                                .content = {
                                    ui::RadioButtons{
                                        .items          = {"Standard", "Compact"},
                                        .selected_index = density,
                                    },
                                    // Everything inside the scope takes its density.
                                    ui::DensityScope{
                                        .density = ui::bind(density, [](std::size_t d) {
                                            return d == 1 ? ui::Density::Compact : ui::Density::Standard;
                                        }),
                                        .content = ui::VerticalStack{
                                            .spacing = ui::bind(density, [](std::size_t d) { return d == 1 ? 8.0f : 16.0f; }),
                                            .content = {
                                                ui::TextBox{.header = "First Name:"},
                                                ui::TextBox{.header = "Last Name:"},
                                            },
                                        },
                                    },
                                },
                            }
                        )cpp",
                    .options  = ui::RadioButtons{.header         = "Fluent Standard and Compact Sizing",
                                                 .items          = {"Standard", "Compact"},
                                                 .selected_index = s.density},
                    .controls = {ui::DensityScope{
                        .density = ui::bind(s.density,
                                            [](std::optional<std::size_t> d)
                                            { return d == 1 ? ui::Density::Compact : ui::Density::Standard; }),
                        .content = ui::
                            VerticalStack{.spacing = ui::bind(s.density,
                                                              [](std::optional<std::size_t> d)
                                                              { return d == 1 ? 8.0f : 16.0f; }),
                                          .content =
                                              {
                                                  ui::Text{.text       = ui::bind(s.density,
                                                                                  [](std::optional<std::size_t> d)
                                                                                  {
                                                                                return std::string{
                                                                                    d == 1 ? "Co"
                                                                                             "mp"
                                                                                             "ac"
                                                                                             "t "
                                                                                             "Si"
                                                                                             "ze"
                                                                                           : "St"
                                                                                             "an"
                                                                                             "da"
                                                                                             "rd"
                                                                                             " S"
                                                                                             "iz"
                                                                                             "e"};
                                                                                  }),
                                                           .text_style = heading},
                                                  ui::TextBox{
                                                      .header = "First "
                                                                "Name:"},
                                                  ui::TextBox{
                                                      .header = "Last "
                                                                "Name:"},
                                              }}}},
                }),
            });
        });
}

// ---- Icon ----

struct IconElementState
{
    ui::Property<bool> monochrome{false};
};

ui::Spec icon_page(Context&)
{
    return with_state<IconElementState>(
        [](IconElementState& s)
        {
            return examples({
                example({
                    .title    = "A symbol in a button",
                    .code     = R"cpp(
                                ui::Button{
                                    .content = ui::VerticalStack{
                                        .content = {
                                            ui::Icon{.source = ui::Symbol::Accept},
                                            ui::Text{.text = "Accept"},
                                        },
                                    },
                                }
                            )cpp",
                    .controls = {ui::VerticalStack{
                        .child_alignment = ui::Alignment::Leading,
                        .content =
                            {
                                paragraph("A Symbol names a glyph of the theme's symbol font and draws at "
                                          "20 DIPs in the text color."),
                                ui::Button{.accessibility = {.name = "AcceptButton"},
                                           .content = ui::VerticalStack{.content =
                                                                            {
                                                                                ui::Icon{.source = ui::Symbol::Accept},
                                                                                ui::Text{.text = "Accept"},
                                                                            }}},
                            },
                    }},
                }),
                example({
                    .title = "A glyph from a specific font in a button",
                    .code  = R"cpp(
                                ui::Button{
                                    .content = ui::Icon{.source = ui::IconGlyph{0xE790, "Segoe MDL2 Assets", 20}},
                                }
                            )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .child_alignment = ui::Alignment::Leading,
                                .content =
                                    {
                                        paragraph("An IconGlyph carries a code point, a font and a size. A glyph from "
                                                  "another font than the theme's symbol font keeps its own font."),
                                        ui::Button{.accessibility = {.name = "ExampleButton1"},
                                                   .content = ui::Icon{.source = ui::IconGlyph{0xE790,
                                                                                               "Segoe MDL2 Assets",
                                                                                               20.0f}}},
                                    },
                            }},
                }),
                example({
                    .title = "A path in a button",
                    .code  = R"cpp(
                                ui::Button{
                                    .content = ui::Icon{
                                        // parse() returns an empty optional for bad path data.
                                        .source = ui::IconSource::path(*ui::PathGeometry::parse("F1 M 16,12 20,2L 20,16 1,16")),
                                        .horizontal_alignment = ui::Alignment::Center,
                                    },
                                }
                            )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .child_alignment = ui::Alignment::Leading,
                                .content =
                                    {
                                        paragraph("A path icon fills its geometry in the text color. It "
                                                  "measures from "
                                                  "the origin to the geometry's far corner."),
                                        ui::Button{.accessibility = {.name = "Example1Button"},
                                                   .content = ui::Icon{.source = ui::IconSource::path(*ui::PathGeometry::parse("F1 M "
                                                                                                                               "16,12 "
                                                                                                                               "20,2L "
                                                                                                                               "20,16 "
                                                                                                                               "1,16")),
                                                                       .horizontal_alignment = ui::Alignment::Center}},
                                    },
                            }},
                }),
                example({
                    .title   = "A monochrome bitmap",
                    .code    = R"cpp(
                            ui::Property<bool> monochrome{true};
                            const ui::Bitmap   slices = /* ... a multicolor image ... */;

                            ui::VerticalStack{
                                .spacing = 8,
                                .content = {
                                    ui::CheckBox{
                                        .text               = "Monochrome",
                                        .checked            = monochrome,
                                        .on_checked_changed = [&](std::optional<bool> v) { monochrome = v.value_or(false); },
                                    },
                                    // Monochrome paints every opaque pixel in the foreground color.
                                    ui::Icon{
                                        .source = ui::bind(monochrome, [&](bool on) { return ui::IconSource::bitmap(slices, on); }),
                                        .size   = 50,
                                        .horizontal_alignment = ui::Alignment::Leading,
                                    },
                                },
                            }
                        )cpp",
                    .options = ui::CheckBox{.text               = "Monochrome",
                                            .checked            = ui::bind_one_way(s.monochrome),
                                            .on_checked_changed = [&s](std::optional<bool> v)
                                            { s.monochrome = v.value_or(false); }},
                    .controls =
                        {ui::
                             VerticalStack{
                                 .child_alignment = ui::Alignment::Leading,
                                 .content =
                                     {
                                         paragraph("IconSource::bitmap() draws a bitmap as a silhouette in the text "
                                                   "color, "
                                                   "or with its own colors when monochrome is off."),
                                         ui::Icon{.source               = ui::bind(s.monochrome,
                                                                                   [](bool on)
                                                                                   {
                                                                         return ui::IconSource::bitmap(slices_image(), on);
                                                                                   }),
                                                  .size                 = 50.0f,
                                                  .horizontal_alignment = ui::Alignment::Leading},
                                     },
                             }},
                }),
                example({
                    .title = "A picture in a button",
                    .code  = R"cpp(
                                const ui::Bitmap slices = /* ... a decoded PNG ... */;

                                ui::Button{
                                    .width   = 100,
                                    .content = ui::Icon{.source = slices},
                                }
                            )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .child_alignment = ui::Alignment::Leading,
                                .content =
                                    {
                                        paragraph("A Bitmap, an SVG document or an animated image shows in its "
                                                  "own colors, fitted into the icon's box."),
                                        ui::Button{.width         = 100.0f,
                                                   .accessibility = {.name = "ImageExample1"},
                                                   .content       = ui::Icon{.source = slices_image()}},
                                    },
                            }},
                }),
                example({
                    .title    = "An SVG picture in a button",
                    .code     = R"cpp(
                            // The SVG document outlives the icon that draws it.
                            static const ui::SvgImage camera{
                                "<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' viewBox='0 0 48 48'>"
                                "<rect x='3' y='12' width='42' height='28' rx='4' fill='#3A96DD'/>"
                                "<circle cx='24' cy='26' r='9' fill='#FFFFFF'/>"
                                "</svg>"};

                            ui::Button{
                                .content = ui::Icon{.source = camera, .size = 50},
                            }
                        )cpp",
                    .controls = {ui::Button{.accessibility = {.name = "ImageExample2"},
                                            .content       = ui::Icon{.source = camera_svg(), .size = 50.0f}}},
                }),
            });
        });
}

// ---- Line ----

// Text at a Canvas position, shown only while `show` is on: the samples' point labels, shown
// while the ToggleSwitch is on.
ui::CanvasItem point_label(ui::Property<bool>& show, float left, float top, const char* text)
{
    return ui::CanvasItem{.left    = left,
                          .top     = top,
                          .z_index = 1,
                          .content = ui::Match<bool>{.value   = show,
                                                     .content = [text](const bool& on) -> ui::Spec
                                                     {
                                                         if (!on)
                                                         {
                                                             return {};
                                                         }
                                                         return ui::Text{.text = text};
                                                     }}};
}

struct LineState
{
    ui::Property<float> x1{0.0f};
    ui::Property<float> y1{0.0f};
    ui::Property<float> x2{200.0f};
    ui::Property<float> y2{0.0f};
    ui::Property<float> thickness{5.0f};

    ui::Property<bool>  polyline_points{false};
    ui::Property<float> polyline_thickness{2.0f};

    ui::Property<bool>  path_points{false};
    ui::Property<float> path_thickness{2.0f};

    ui::Property<float>            radius_x{30.0f};
    ui::Property<float>            radius_y{30.0f};
    ui::Property<ui::PathGeometry> group{};

    void update_group()
    {
        const std::array<ui::PathGeometry, 3>
            parts{ui::PathGeometry::line(ui::Point{10.0f, 10.0f}, ui::Point{50.0f, 30.0f}),
                  ui::PathGeometry::ellipse(ui::Point{40.0f, 70.0f}, radius_x.get(), radius_y.get()),
                  ui::PathGeometry::rectangle(ui::Rect{30.0f, 55.0f, 100.0f, 30.0f})};
        group = ui::PathGeometry::group(parts, ui::FillRule::EvenOdd);
    }
};

ui::Spec line_page(Context&)
{
    return with_state<LineState>(
        [](LineState& s)
        {
            s.update_group();
            const auto identity = [](float v) { return v; };
            return examples({
                example({
                    .title   = "Line",
                    .code    = R"cpp(
                        ui::Property<float> x2{200.0f};
                        ui::Property<float> thickness{5.0f};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Slider{.header = "End point X", .value = x2, .min = 200.0f, .max = 300.0f},
                                ui::Slider{.header = "Stroke Thickness", .value = thickness, .min = 5.0f, .max = 10.0f},
                                // From (x1, y1) to (x2, y2), in the line's own coordinates.
                                ui::Line{
                                    .x1               = 0.0f,
                                    .y1               = 0.0f,
                                    .x2               = x2,
                                    .y2               = 0.0f,
                                    .stroke           = ui::Color::from_argb(0xFF4682B4),
                                    .stroke_thickness = thickness,
                                },
                            },
                        }
                    )cpp",
                    .options = options_panel(220.0f,
                                             {
                                                 slider("Start point X", 0.0f, 100.0f, s.x1),
                                                 slider("Start point Y", 0.0f, 100.0f, s.y1),
                                                 slider("End point X", 200.0f, 300.0f, s.x2),
                                                 slider("End point Y", 0.0f, 100.0f, s.y2),
                                                 slider("Stroke Thickness", 5.0f, 10.0f, s.thickness, 0.5f, 1.0f),
                                             }),
                    .controls = {ui::Canvas{.width    = 100.0f,
                                            .height   = 200.0f,
                                            .content  = {ui::CanvasItem{.top     = 50.0f,
                                                          .content = ui::Line{.x1     = ui::bind(s.x1, identity),
                                                                              .y1     = ui::bind(s.y1, identity),
                                                                              .x2     = ui::bind(s.x2, identity),
                                                                              .y2     = ui::bind(s.y2, identity),
                                                                              .stroke = SteelBlue,
                                                                              .stroke_thickness = ui::bind(s.thickness, identity)}}}}},
                }),
                example({
                    .title   = "Polyline",
                    .code    = R"cpp(
                        ui::Property<float> thickness{2.0f};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Slider{.header = "Stroke Thickness", .value = thickness, .min = 2.0f, .max = 10.0f},
                                // Connected straight lines; the last point is not joined to the first.
                                ui::Polyline{
                                    .points           = std::vector<ui::Point>{{10.0f, 100.0f}, {60.0f, 40.0f}, {200.0f, 40.0f}, {250.0f, 100.0f}},
                                    .stroke           = ui::colors::Black,
                                    .stroke_thickness = thickness,
                                },
                            },
                        }
                    )cpp",
                    .options = options_panel(220.0f,
                                             {
                                                 ui::ToggleSwitch{.header     = "Show points",
                                                                  .checked         = s.polyline_points},
                                                 slider("Stroke Thickness", 2.0f, 10.0f, s.polyline_thickness, 0.5f, 1.0f),
                                             }),
                    .controls = {ui::Canvas{
                        .width  = 320.0f,
                        .height = 170.0f,
                        .content =
                            {
                                ui::CanvasItem{.content = ui::VerticalStack{.content =
                                                             {
                                                                 ui::Text{.text   = "Draws a series of connected straight lines.",
                                                                          .margin = ui::Edges{.bottom = 10.0f}},
                                                                 ui::Polyline{.points = std::vector<ui::Point>{{10.0f, 100.0f},
                                                                                                           {60.0f, 40.0f},
                                                                                                           {200.0f, 40.0f},
                                                                                                           {250.0f, 100.0f}},
                                                                              .stroke = ui::colors::Black,
                                                                              .stroke_thickness = ui::bind(s.polyline_thickness, identity)},
                                                             }}},
                                point_label(s.polyline_points, 0.0f, 140.0f, "Point #1: (10,100)"),
                                point_label(s.polyline_points, 50.0f, 40.0f, "Point #2: (60,40)"),
                                point_label(s.polyline_points, 200.0f, 40.0f, "Point #3: (200,40)"),
                                point_label(s.polyline_points, 240.0f, 140.0f, "Point #4: (250,100)"),
                            },
                    }},
                }),
                example({
                    .title   = "Path",
                    .code    = R"cpp(
                        ui::Property<float> thickness{2.0f};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Slider{.header = "Stroke Thickness", .value = thickness, .min = 2.0f, .max = 10.0f},
                                // Lines and curves from path data; parse() returns an empty optional for bad data.
                                ui::Path{
                                    .data             = *ui::PathGeometry::parse("M 10,100 C 100,25 300,250 400,75 H 200"),
                                    .stroke           = ui::Color::from_argb(0xFFB8860B),
                                    .stroke_thickness = thickness,
                                },
                            },
                        }
                    )cpp",
                    .options = options_panel(220.0f,
                                             {
                                                 ui::ToggleSwitch{.header     = "Show points",
                                                                  .checked         = s.path_points},
                                                 slider("Stroke Thickness", 2.0f, 10.0f, s.path_thickness, 0.5f, 1.0f),
                                             }),
                    .controls = {ui::Canvas{
                        .width  = 320.0f,
                        .height = 200.0f,
                        .content =
                            {
                                ui::CanvasItem{.content = ui::VerticalStack{.content =
                                                             {
                                                                 ui::Text{.text = "Draws a series of connected lines and curves."},
                                                                 ui::Path{.data   = *ui::PathGeometry::parse("M 10,100 C 100,25 300,250 400,75 H 200"),
                                                                          .stroke = DarkGoldenrod,
                                                                          .stroke_thickness = ui::bind(s.path_thickness, identity)},
                                                             }}},
                                point_label(s.path_points, 0.0f, 130.0f, "Point #1: (10,100)"),
                                point_label(s.path_points, 40.0f, 75.0f, "Point #2: (100,25)"),
                                point_label(s.path_points, 280.0f, 175.0f, "Point #3: (300,250)"),
                                point_label(s.path_points, 360.0f, 60.0f, "Point #4: (400,75)"),
                                point_label(s.path_points, 170.0f, 60.0f, "Point #5: (200,75)"),
                            },
                    }},
                }),
                example({
                    .title   = "A geometry group",
                    .code    = R"cpp(
                        ui::Property<float>        radius_x{30.0f};
                        ui::Property<ui::PathGeometry> group{};

                        auto update = [&]
                        {
                            const std::array<ui::PathGeometry, 3> parts{
                                ui::PathGeometry::line(ui::Point{10.0f, 10.0f}, ui::Point{50.0f, 30.0f}),
                                ui::PathGeometry::ellipse(ui::Point{40.0f, 70.0f}, radius_x.get(), 30.0f),
                                ui::PathGeometry::rectangle(ui::Rect{30.0f, 55.0f, 100.0f, 30.0f}),
                            };
                            group = ui::PathGeometry::group(parts, ui::FillRule::EvenOdd);
                        };
                        update();

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Slider{
                                    .header           = "Radius X",
                                    .value            = radius_x,
                                    .min              = 30.0f,
                                    .max              = 50.0f,
                                    .on_value_changed = [&](double) { update(); },
                                },
                                ui::Path{
                                    .data             = group,
                                    .fill             = ui::Color::from_argb(0xFFCCCCFF),
                                    .stroke           = ui::colors::Black,
                                    .stroke_thickness = 4.0f,
                                },
                            },
                        }
                    )cpp",
                    .options = options_panel(220.0f,
                                             {
                                                 ui::Slider{.header           = "Radius X",
                                                            .value            = s.radius_x,
                                                            .min              = 30.0f,
                                                            .max              = 50.0f,
                                                            .step_frequency   = 0.5f,
                                                            .on_value_changed =
                                                                [&s](double v)
                                                            {
                                                                s.radius_x = static_cast<float>(static_cast<float>(v));
                                                                s.update_group();
                                                            }},
                                                 ui::Slider{.header           = "Radius Y",
                                                            .value            = s.radius_y,
                                                            .min              = 30.0f,
                                                            .max              = 50.0f,
                                                            .step_frequency   = 0.5f,
                                                            .on_value_changed =
                                                                [&s](double v)
                                                            {
                                                                s.radius_y = static_cast<float>(static_cast<float>(v));
                                                                s.update_group();
                                                            }},
                                             }),
                    .controls = {ui::Canvas{
                        .width  = 100.0f,
                        .height = 170.0f,
                        .content =
                            {
                                ui::CanvasItem{.content = ui::VerticalStack{.content =
                                                             {
                                                                 ui::Text{.text   = "Composite geometry objects can be created "
                                                                                    "using PathGeometry::group().",
                                                                          .margin = ui::Edges{.bottom = 15.0f}},
                                                                 ui::Path{.data             = s.group,
                                                                          .fill             = Lavender,
                                                                          .stroke           = ui::colors::Black,
                                                                          .stroke_thickness = 4.0f},
                                                             }}},
                            },
                    }},
                }),
            });
        });
}

// ---- Shape ----

struct ShapeState
{
    ui::Property<float> ellipse_height{100.0f};
    ui::Property<float> ellipse_width{100.0f};
    ui::Property<float> ellipse_thickness{2.0f};

    ui::Property<float> rect_height{100.0f};
    ui::Property<float> rect_width{100.0f};
    ui::Property<float> rect_thickness{2.0f};
    ui::Property<float> rect_radius_y{0.0f};
    ui::Property<float> rect_radius_x{0.0f};

    ui::Property<bool>  polygon_points{false};
    ui::Property<float> polygon_thickness{2.0f};
};

ui::Spec shape_page(Context&)
{
    return with_state<ShapeState>(
        [](ShapeState& s)
        {
            const auto identity = [](float v) { return v; };

            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title = "Ellipse",
                    .code  = R"cpp(
                                    ui::Property<float> width{100.0f};
                                    ui::Property<float> thickness{2.0f};

                                    ui::VerticalStack{
                                        .spacing = 8,
                                        .content = {
                                            ui::Slider{.header = "Width", .value = width, .min = 100.0f, .max = 150.0f},
                                            ui::Slider{.header = "Stroke Thickness", .value = thickness, .min = 2.0f, .max = 10.0f},
                                            ui::Ellipse{
                                                .fill             = ui::Color::from_argb(0xFF4682B4),
                                                .stroke           = ui::colors::Black,
                                                .stroke_thickness = thickness,
                                                .width            = width,
                                                .height           = 100,
                                            },
                                        },
                                    }
                                )cpp",
                    .options = options_panel(220.0f,
                                             {
                                                 slider("Height", 100.0f, 150.0f, s.ellipse_height),
                                                 slider("Width", 100.0f, 150.0f, s.ellipse_width),
                                                 slider("Stroke Thickness", 2.0f, 10.0f, s.ellipse_thickness, 0.5f, 1.0f),
                                             }),
                    .controls = {ui::Ellipse{.fill             = SteelBlue,
                                             .stroke           = ui::colors::Black,
                                             .stroke_thickness = s.ellipse_thickness,
                                             .width            = s.ellipse_width,
                                             .height           = s.ellipse_height}},
                }),
                example({
                    .title    = "Rectangle",
                    .code     = R"cpp(
                                        ui::Property<float> thickness{2.0f};
                                        ui::Property<float> radius_x{0.0f};
                                        ui::Property<float> radius_y{0.0f};

                                        ui::VerticalStack{
                                            .spacing = 8,
                                            .content = {
                                                ui::Slider{.header = "Stroke Thickness", .value = thickness, .min = 2.0f, .max = 10.0f},
                                                ui::Slider{.header = "Radius X", .value = radius_x, .min = 0.0f, .max = 100.0f},
                                                ui::Slider{.header = "Radius Y", .value = radius_y, .min = 0.0f, .max = 100.0f},
                                                ui::Rectangle{
                                                    .fill             = ui::Color::from_argb(0xFF4682B4),
                                                    .stroke           = ui::colors::Black,
                                                    .stroke_thickness = thickness,
                                                    .radius_x         = radius_x, // rounded corners
                                                    .radius_y         = radius_y,
                                                    .width            = 100,
                                                    .height           = 100,
                                                },
                                            },
                                        }
                                    )cpp",
                    .options  = options_panel(220.0f,
                                              {
                                                  slider("Height", 100.0f, 150.0f, s.rect_height),
                                                  slider("Width", 100.0f, 150.0f, s.rect_width),
                                                  slider("Stroke Thickness", 2.0f, 10.0f, s.rect_thickness, 0.5f, 1.0f),
                                                  slider("Radius Y", 0.0f, 100.0f, s.rect_radius_y),
                                                  slider("Radius X", 0.0f, 100.0f, s.rect_radius_x),
                                              }),
                    .controls = {ui::Rectangle{.fill             = SteelBlue,
                                               .stroke           = ui::colors::Black,
                                               .stroke_thickness = ui::bind(s.rect_thickness, identity),
                                               .radius_x         = ui::bind(s.rect_radius_x, identity),
                                               .radius_y         = ui::bind(s.rect_radius_y, identity),
                                               .width            = s.rect_width,
                                               .height           = s.rect_height}},
                }),
                example({
                    .title = "Polygon",
                    .code  = R"cpp(
                                        ui::Property<float> thickness{2.0f};

                                        ui::VerticalStack{
                                            .spacing = 8,
                                            .content = {
                                                ui::Slider{.header = "Stroke Thickness", .value = thickness, .min = 2.0f, .max = 10.0f},
                                                // The last point is joined to the first, closing the shape.
                                                ui::Polygon{
                                                    .points           = std::vector<ui::Point>{{10.0f, 100.0f}, {60.0f, 40.0f}, {200.0f, 40.0f}, {250.0f, 100.0f}},
                                                    .fill             = ui::Color::from_argb(0xFF4682B4),
                                                    .stroke           = ui::colors::Black,
                                                    .stroke_thickness = thickness,
                                                },
                                            },
                                        }
                                    )cpp",
                    .options = options_panel(220.0f,
                                             {
                                                 ui::ToggleSwitch{.header = "Show points", .checked = s.polygon_points},
                                                 slider("Stroke Thickness", 2.0f, 10.0f, s.polygon_thickness, 0.5f, 1.0f),
                                             }),
                    .controls = {ui::
                                     Canvas{
                                         .width  = 320.0f,
                                         .height = 200.0f,
                                         .content =
                                             {
                                                 ui::CanvasItem{.content = ui::VerticalStack{.content =
                                                                                                 {
                                                                                                     ui::Text{.text = "A polygon is a connected series "
                                                                                                                      "of lines "
                                                                                                                      "that form a closed shape.",
                                                                                                              .margin = ui::Edges{.bottom = 15.0f}},
                                                                                                     ui::Polygon{.points = std::vector<
                                                                                                                     ui::Point>{{10.0f, 100.0f}, {60.0f, 40.0f}, {200.0f, 40.0f}, {250.0f, 100.0f}},
                                                                                                                 .fill = SteelBlue,
                                                                                                                 .stroke = ui::colors::Black,
                                                                                                                 .stroke_thickness = ui::bind(s.polygon_thickness, identity)},
                                                                                                 }}},
                                                 point_label(s.polygon_points, 0.0f, 150.0f, "Point #1: (10,100)"),
                                                 point_label(s.polygon_points, 50.0f, 40.0f, "Point #2: (60,40)"),
                                                 point_label(s.polygon_points, 200.0f, 40.0f, "Point #3: (200,40)"),
                                                 point_label(s.polygon_points, 240.0f, 150.0f, "Point #4: (250,100)"),
                                             },
                                     }},
                }),
            }));
            return ui::Spec{std::move(page)};
        });
}

// ---- RadialGradientBrush ----

struct RadialState
{
    ui::Property<std::optional<std::size_t>> mapping{0}; // RelativeToBoundingBox, Absolute
    ui::Property<std::optional<std::size_t>> spread{0};  // Pad, Reflect, Repeat
    ui::Property<float>                      center_x{0.5f};
    ui::Property<float>                      center_y{0.5f};
    ui::Property<float>                      radius_x{0.5f};
    ui::Property<float>                      radius_y{0.5f};
    ui::Property<float>                      origin_x{0.5f};
    ui::Property<float>                      origin_y{0.5f};
    ui::Property<ui::Paint>                  fill{};

    void update()
    {
        ui::RadialGradientBrush brush;
        brush.stops           = {{0.0f, Yellow}, {1.0f, Blue}};
        brush.center          = ui::Point{center_x.get(), center_y.get()};
        brush.radius_x        = radius_x.get();
        brush.radius_y        = radius_y.get();
        brush.gradient_origin = ui::Point{origin_x.get(), origin_y.get()};
        brush.mapping_mode    = mapping.get().value_or(0) == 1 ? ui::BrushMappingMode::Absolute
                                                               : ui::BrushMappingMode::RelativeToBoundingBox;
        brush.spread_method   = spread.get().value_or(0) == 1   ? ui::GradientSpreadMethod::Reflect
                                : spread.get().value_or(0) == 2 ? ui::GradientSpreadMethod::Repeat
                                                                : ui::GradientSpreadMethod::Pad;
        fill                  = brush;
    }
};

// One of the six sliders: 0..1 in steps of 0.02 relative, 0..200 (the rectangle's width) in steps
// of 4 absolute.
ui::Spec radial_slider(RadialState& s, const char* header, ui::Property<float>& value, bool absolute)
{
    return ui::Slider{.header           = header,
                      .value            = value,
                      .min              = 0.0f,
                      .max              = absolute ? 200.0f : 1.0f,
                      .step             = absolute ? 10.0f : 0.05f,
                      .step_frequency   = absolute ? 4.0f : 0.02f,
                      .on_value_changed = [&s, &value](double v)
                      {
                          value = static_cast<float>(static_cast<float>(v));
                          s.update();
                      }};
}

ui::Spec radial_gradient_brush_page(Context&)
{
    return with_state<RadialState>(
        [](RadialState& s)
        {
            s.update();
            return example({
                .title = "RadialGradientBrush Sample",
                .code  = R"cpp(
                    ui::Property<float>                    center_x{0.5f};
                    ui::Property<std::optional<std::size_t>>              spread{0}; // Pad, Reflect, Repeat
                    ui::Property<ui::Paint> fill{};

                    auto update = [&]
                    {
                        ui::RadialGradientBrush brush;
                        brush.stops           = {{0.0f, ui::Color::from_argb(0xFFFFFF00)}, {1.0f, ui::Color::from_argb(0xFF0000FF)}};
                        brush.center          = ui::Point{center_x.get(), 0.5f};
                        brush.radius_x        = 0.5f;
                        brush.radius_y        = 0.5f;
                        brush.gradient_origin = ui::Point{0.5f, 0.5f};
                        // Relative: 0..1 across the shape. Absolute takes DIPs instead.
                        brush.mapping_mode  = ui::BrushMappingMode::RelativeToBoundingBox;
                        brush.spread_method = spread.get().value_or(0) == 1   ? ui::GradientSpreadMethod::Reflect
                                              : spread.get().value_or(0) == 2 ? ui::GradientSpreadMethod::Repeat
                                                                  : ui::GradientSpreadMethod::Pad;
                        fill                = brush;
                    };
                    update();

                    ui::VerticalStack{
                        .spacing = 8,
                        .content = {
                            ui::Slider{
                                .header           = "Center X",
                                .value            = center_x,
                                .min              = 0.0f,
                                .max              = 1.0f,
                                .step_frequency   = 0.02f,
                                .on_value_changed = [&](double) { update(); },
                            },
                            ui::ComboBox{
                                .header               = "Spread method",
                                .items                = {"Pad", "Reflect", "Repeat"},
                                .selected_index       = spread,
                                .on_selection_changed = [&](std::optional<std::size_t>) { update(); },
                            },
                            ui::Rectangle{.fill = fill, .width = 200, .height = 200},
                        },
                    }
                )cpp",
                .options =
                    ui::VerticalStack{
                        .spacing = 8.0f,
                        .width   = 300.0f,
                        .content =
                            {
                                ui::ComboBox{.header         = "Mapping mode",
                                             .items          = {"RelativeToBoundingBox", "Absolute"},
                                             .selected_index = s.mapping,
                                             .on_selection_changed =
                                                 [&s](std::optional<std::size_t> i)
                                             {
                                                 // Every slider back to the middle of its new range.
                                                 const float mid = i == 1 ? 100.0f : 0.5f;
                                                 for (ui::Property<float>* p :
                                                      {&s.center_x, &s.center_y, &s.radius_x, &s.radius_y, &s.origin_x, &s.origin_y})
                                                 {
                                                     *p = mid;
                                                 }
                                                 s.mapping = i.value_or(0);
                                                 s.update();
                                             }},
                                ui::Match<std::size_t>{
                                    .value   = ui::bind(s.mapping, [](std::optional<std::size_t> m) { return m.value_or(0); }),
                                    .content = [&s](const std::size_t& mapping) -> ui::Spec
                                    {
                                        const bool absolute = mapping == 1;
                                        return ui::
                                            Grid{.columns        = {1_fr, 1_fr},
                                                 .rows           = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                                 .column_spacing = 8.0f,
                                                 .content          = {
                                                     ui::Cell{.row     = 0,
                                                      .column  = 0,
                                                      .content = radial_slider(s, "Center X", s.center_x, absolute)},
                                                     ui::Cell{.row     = 0,
                                                      .column  = 1,
                                                      .content = radial_slider(s, "Center Y", s.center_y, absolute)},
                                                     ui::Cell{.row     = 1,
                                                      .column  = 0,
                                                      .content = radial_slider(s, "Radius X", s.radius_x, absolute)},
                                                     ui::Cell{.row     = 1,
                                                      .column  = 1,
                                                      .content = radial_slider(s, "Radius Y", s.radius_y, absolute)},
                                                     ui::Cell{.row    = 2,
                                                      .column = 0,
                                                      .content = radial_slider(s, "Gradient origin X", s.origin_x, absolute)},
                                                     ui::Cell{.row    = 2,
                                                      .column = 1,
                                                      .content = radial_slider(s, "Gradient origin Y", s.origin_y, absolute)},
                                                 }};
                                    }},
                                ui::ComboBox{.header         = "Spread method",
                                             .items          = {"Pad", "Reflect", "Repeat"},
                                             .selected_index = s.spread,
                                             .margin         = ui::Edges{.top = 10.0f},
                                             .on_selection_changed =
                                                 [&s](std::optional<std::size_t> i)
                                             {
                                                 s.spread = i;
                                                 s.update();
                                             }},
                            },
                    },
                .controls = {ui::Rectangle{.fill    = s.fill,
                                           .width   = 200.0f,
                                           .height  = 200.0f,
                                           .horizontal_alignment = ui::Alignment::Center,
                                           .vertical_alignment = ui::Alignment::Center}},
            });
        });
}

// ---- Backdrops ----

ui::Spec backdrops_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return example({
        .title    = "Backdrop types",
        .code     = R"cpp(
            // `window` is the ui::Window the content is in.
            ui::RadioButtons{
                .header               = "This window's backdrop",
                .items                = {"None", "Mica", "Mica Alt", "Desktop Acrylic"},
                .selected_index       = static_cast<int>(window.backdrop()),
                .on_selection_changed = [&window](std::optional<std::size_t> i)
                {
                    constexpr std::array<ui::Backdrop, 4> Kinds{
                        ui::Backdrop::None, ui::Backdrop::Mica, ui::Backdrop::MicaAlt, ui::Backdrop::Acrylic};
                    if (i.value_or(0) >= 0 && i.value_or(0) < 4)
                    {
                        window.set_backdrop(Kinds[static_cast<std::size_t>(i.value_or(0))]);
                    }
                },
            }
        )cpp",
        .options  = ui::RadioButtons{.header         = "This window's backdrop",
                                     .items          = {"None", "Mica", "Mica Alt", "Desktop Acrylic"},
                                     .selected_index = static_cast<int>(window.backdrop()),
                                     .on_selection_changed =
                                         [&window](std::optional<std::size_t> i)
                                     {
                                        constexpr std::array<ui::Backdrop, 4> Kinds{ui::Backdrop::None,
                                                                                    ui::Backdrop::Mica,
                                                                                    ui::Backdrop::MicaAlt,
                                                                                    ui::Backdrop::Acrylic};
                                        if (i.value_or(0) < 4)
                                        {
                                            window.set_backdrop(Kinds[static_cast<std::size_t>(i.value_or(0))]);
                                        }
                                     }},
        .controls = {ui::VerticalStack{
            .content =
                {
                    paragraph("A window can use one of the following system backdrops:\n"
                              "1. Mica: An opaque material that samples the desktop wallpaper once to tint the "
                              "window background. Best for main app windows.\n"
                              "2. Mica Alt: A variant of Mica with stronger tinting. Recommended for apps with a "
                              "tabbed title bar.\n"
                              "3. Desktop Acrylic: A semi-transparent material that shows a blurred view of the "
                              "content behind the window.\n\n"
                              "Mica vs. Acrylic: Mica is opaque and renders the desktop wallpaper within the window "
                              "background. Desktop Acrylic is semi-transparent and reveals a blurred view of what is "
                              "behind the window in real-time. Mica is more performant because it captures the "
                              "wallpaper only once, while Acrylic updates continuously.\n\n"
                              "A libwinui window takes one through its backdrop option, set when it is created or "
                              "changed later: Backdrop::Mica, Backdrop::MicaAlt or Backdrop::Acrylic. All of "
                              "them need Windows 11 22H2 or later and the Fluent 2 theme, every earlier era keeps "
                              "its opaque window background. In-app acrylic (AcrylicBrush) is a separate brush for "
                              "drawing inside the window.\n\n"
                              "The choice beside this applies to this window: the backdrop shows wherever the gallery "
                              "leaves the window background bare, as behind the navigation pane."),
                },
        }},
    });
}

// ---- Shadow ----

// The sample's Border in the default card background: a
// ui::BorderControl whose background is re-read from the theme before every frame it draws.
class ThemedCardBorder final : public ui::ContentHost
{
public:
    ThemedCardBorder(ui::Property<float>& depth)
    {
        border_ = set_child(std::make_unique<ui::BorderControl>());
        border_->set_width(200.0f);
        border_->set_height(200.0f);
        border_->set_shadow(true);
        border_->set_elevation(depth.get());
        border_->add_subscription(depth.observe([b = border_](const float& z) { b->set_elevation(z); }));
        set_margin(ui::Thickness{36.0f}); // the sample Grid's Padding
    }

    ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
    {
        if (border_->corner_radius() != context.theme.overlay_corner_radius)
        {
            border_->set_corner_radius(context.theme.overlay_corner_radius);
        }
        const bool      fluent = (context.theme.variant == ui::ThemeVariant::Fluent2 ||
                                  context.theme.variant == ui::ThemeVariant::Fluent) &&
                                 !context.theme.high_contrast;
        const ui::Color fill   = fluent ? context.theme.palette.card_background_default : context.theme.colors().field;
        if (border_->background() != std::optional<ui::Color>{fill})
        {
            border_->set_background(fill);
        }
        return ui::ContentHost::measure_override(available, context);
    }

private:
    ui::BorderControl* border_ = nullptr;
};

struct ShadowState
{
    ui::Property<float> depth{32.0f};
};

ui::Spec shadow_page(Context&)
{
    return with_state<ShadowState>(
        [](ShadowState& s)
        {
            return example({
                .title    = "A shadow on a Border",
                .code     = R"cpp(
                    ui::Property<float> depth{32.0f};

                    ui::VerticalStack{
                        .spacing = 8,
                        .content = {
                            ui::Slider{
                                .header = "Z-translation",
                                .value  = depth,
                                .min    = 0.0f,
                                .max    = 64.0f,
                                .width  = 200,
                            },
                            // The shadow grows with the height the border is lifted to.
                            ui::Border{
                                .background    = ui::Theme::light().palette.card_background_default,
                                .corner_radius = 8.0f,
                                .shadow  = true,
                                .elevation = depth,
                                .margin        = ui::Thickness{36.0f}, // room for the shadow
                                .width         = 200,
                                .height        = 200,
                            },
                        },
                    }
                )cpp",
                .options  = ui::Slider{.header               = "Z-translation",
                                       .value                = s.depth,
                                       .min                  = 0.0f,
                                       .max                  = 64.0f,
                                       .width                = 200.0f,
                                       .horizontal_alignment = ui::Alignment::Leading,
                                       .accessibility        = {.name = "shadow intensity"}},
                .controls = {ui::Spec{std::unique_ptr<ui::Element>(std::make_unique<ThemedCardBorder>(s.depth))}},
            });
        });
}

} // namespace

std::span<const Page> styles_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Acrylic", "A translucent material recommended for panel backgrounds.", acrylic_page},
        {"AnimatedIcon",
         "An element that displays and controls an icon that animates when the user interacts with the control.",
         animated_icon_page},
        {"DensityScope", "A scope that makes the controls inside it compact.", density_scope_page},
        {"Icon", "Represents icon controls that use different image types as its content.", icon_page},
        {"Line", "Draws a straight line between two points.", line_page},
        {"Shape", "How to draw shapes, such as ellipses, rectangles, and polygons.", shape_page},
        {"RadialGradientBrush", "A brush to show radial gradients.", radial_gradient_brush_page},
        {"Backdrops", "Mica and acrylic behind a window's content.", backdrops_page},
        {"Shadow", "A drop shadow that deepens with an element's elevation.", shadow_page},
    });
    return Pages;
}

} // namespace gallery

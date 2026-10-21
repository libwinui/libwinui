// SPDX-License-Identifier: MIT
#include "pages_motion.hpp"

#include <array>
#include <memory>
#include <optional>

#include <cstddef>

namespace gallery
{
namespace
{
// ---- Implicit Transitions ----

struct ImplicitState
{
    ui::Control<ui::NumberBox> opacity_box;
    ui::Control<ui::NumberBox> rotation_box;
    ui::Control<ui::NumberBox> scale_box;
    ui::Control<ui::NumberBox> translation_box;
    ui::Control<ui::CheckBox>  scale_x;
    ui::Control<ui::CheckBox>  scale_y;
    ui::Control<ui::CheckBox>  scale_z;
    ui::Control<ui::CheckBox>  translate_x;
    ui::Control<ui::CheckBox>  translate_y;
    ui::Control<ui::CheckBox>  translate_z;
    ThemedShape*               opacity_rect   = nullptr;
    ThemedShape*               rotation_rect  = nullptr;
    ThemedShape*               scale_rect     = nullptr;
    ThemedShape*               translate_rect = nullptr;
    ui::Control<ui::Border>    brush_presenter;
    ui::Control<ui::Border>    theme_grid;
};

ui::Vector3Components components_of(const ui::CheckBoxControl* x, const ui::CheckBoxControl* y, const ui::CheckBoxControl* z)
{
    ui::Vector3Components c = ui::Vector3Components::None;
    if (x->checked() == true)
    {
        c = c | ui::Vector3Components::X;
    }
    if (y->checked() == true)
    {
        c = c | ui::Vector3Components::Y;
    }
    if (z->checked() == true)
    {
        c = c | ui::Vector3Components::Z;
    }
    return c;
}

// The Scale and Translation cards' options: three preset buttons, the components, and a custom value.
ui::Spec vector_options(const char*                               preset_format,
                        std::array<float, 3>                      presets,
                        std::array<ui::Control<ui::CheckBox>*, 3> components,
                        const char*                               header,
                        double                                    max,
                        ui::Control<ui::NumberBox>&               box,
                        const char*                               custom,
                        std::function<void(std::optional<float>)> apply)
{
    ui::SpecList children;
    for (float preset : presets)
    {
        children.push_back(ui::Button{.text = std::vformat(preset_format, std::make_format_args(preset, preset, preset)),
                                      .margin               = ui::Thickness{5.0f},
                                      .horizontal_alignment = ui::Alignment::Stretch,
                                      .on_click             = [apply, preset] { apply(preset); }});
    }
    children.push_back(ui::Text{.text = "Components", .horizontal_alignment = ui::Alignment::Leading});
    const std::array<const char*, 3> Names{"Animate X", "Animate Y", "Animate Z"};
    for (std::size_t i = 0; i < 3; ++i)
    {
        children.push_back(ui::CheckBox{.text = Names[i], .checked = std::optional<bool>{true}, .control = components[i]});
    }
    children.push_back(
        ui::NumberBox{.header           = header,
                      .value            = 1.0,
                      .min              = 0.0,
                      .max              = max,
                      .margin           = ui::Thickness{5.0f},
                      .on_value_changed = [apply](std::optional<double>) { apply(std::nullopt); },
                      .control          = box});
    children.push_back(ui::Button{.text                 = custom,
                                  .margin               = ui::Thickness{5.0f},
                                  .horizontal_alignment = ui::Alignment::Stretch,
                                  .on_click             = [apply] { apply(std::nullopt); }});
    return ui::VerticalStack{.child_alignment = ui::Alignment::Stretch, .width = 240.0f, .content = std::move(children)};
}

} // namespace

ui::Spec implicit_transitions_page(Context&)
{
    return with_state<ImplicitState>(
        [](ImplicitState& s)
        {
            auto gradient = std::make_unique<ThemedShape>(ThemedShape::Kind::Rectangle, system_accent, 50.0f, 50.0f);
            gradient->set_gradient_top(LightGray);
            gradient->set_margin(ui::Edges{.left = 45.0f, .top = 5.0f, .right = 5.0f, .bottom = 5.0f});
            gradient->set_vertical_alignment(ui::Alignment::Center);
            s.rotation_rect = gradient.get();

            auto opacity = accent_rectangle(50.0f,
                                            &s.opacity_rect,
                                            ui::Edges{.left = 45.0f, .top = 5.0f, .right = 5.0f, .bottom = 5.0f});
            opacity->set_vertical_alignment(ui::Alignment::Center);
            auto scale = accent_rectangle(50.0f,
                                          &s.scale_rect,
                                          ui::Edges{.left = 45.0f, .top = 5.0f, .right = 5.0f, .bottom = 5.0f});
            scale->set_vertical_alignment(ui::Alignment::Leading);
            auto translate = accent_rectangle(50.0f,
                                              &s.translate_rect,
                                              ui::Edges{.left = 45.0f, .top = 5.0f, .right = 5.0f, .bottom = 5.0f});
            translate->set_vertical_alignment(ui::Alignment::Leading);

            const auto apply_scale = [&s](std::optional<float> preset)
            {
                // The transition animates only the ticked components.
                const float value = preset ? *preset : value_or_zero(s.scale_box);
                s.scale_rect->set_scale_transition(
                    ui::Vector3Transition{.components = components_of(s.scale_x, s.scale_y, s.scale_z)});
                s.scale_rect->set_scale(ui::Vector3{value, value, value});
            };
            // Rotate about the rectangle's center, clockwise.
            const auto apply_rotation = [&s]
            {
                const ui::Size size = s.rotation_rect->actual_size();
                s.rotation_rect->set_center_point(ui::Vector3{size.width / 2.0f, size.height / 2.0f, 0.0f});
                s.rotation_rect->set_rotation(value_or_zero(s.rotation_box));
            };
            const auto apply_translation = [&s](std::optional<float> preset)
            {
                const float value = preset ? *preset : value_or_zero(s.translation_box);
                s.translate_rect->set_translation_transition(
                    ui::Vector3Transition{.components = components_of(s.translate_x, s.translate_y, s.translate_z)});
                s.translate_rect->set_translation(ui::Vector3{value, value, value});
            };

            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title   = "Automatically animate changes to Opacity",
                    .code    = R"cpp(
                        ui::Control<ui::Rectangle> rect;
                        const ui::Color            accent = window.theme().accent_ramp.base;

                        ui::HorizontalStack{
                            .spacing = 12,
                            .content = {
                                ui::Rectangle{.fill = accent, .width = 50, .height = 50, .control = rect},
                                ui::NumberBox{
                                    .header           = "Opacity (0.0 to 1.0)",
                                    .value            = 0.5,
                                    .min              = 0.0,
                                    .max              = 1.0,
                                    .on_value_changed = [&](std::optional<double> value) { rect->set_opacity(static_cast<float>(value.value_or(0.0))); },
                                },
                            },
                        }

                        // Once built: from now on every set_opacity() fades instead of jumping.
                        rect->set_opacity(0.5f);
                        rect->set_opacity_transition(ui::ScalarTransition{});
                    )cpp",
                    .options = ui::VerticalStack{
                        .child_alignment = ui::Alignment::Stretch,
                        .width       = 240.0f,
                        .content =
                            {
                                ui::NumberBox{.header = "Opacity (0.0 to 1.0)",
                                              .value  = 0.5,
                                              .min    = 0.0,
                                              .max    = 1.0,
                                              .margin = ui::Thickness{5.0f},
                                              // NumberBox_KeyDown: Enter applies it too (so
                                              // does leaving the box, which commits it).
                                              .on_value_changed = [&s](std::optional<double>)
                                              { s.opacity_rect->set_opacity(value_or_zero(s.opacity_box)); },
                                              .control = s.opacity_box},
                                ui::Button{.text     = "Set Opacity",
                                           .margin   = ui::Thickness{5.0f},
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click = [&s] { s.opacity_rect->set_opacity(value_or_zero(s.opacity_box)); }},
                            }},
                    .controls = {std::move(opacity)},
                }),
                example({
                    .title   = "Automatically animate changes to Rotation",
                    .code    = R"cpp(
                        ui::Control<ui::Rectangle> rect;
                        const ui::Color            accent = window.theme().accent_ramp.base;

                        ui::HorizontalStack{
                            .spacing = 12,
                            .content = {
                                ui::Rectangle{.fill = accent, .width = 50, .height = 50, .control = rect},
                                ui::NumberBox{
                                    .header           = "Rotation (0.0 to 360.0)",
                                    .value            = 45.0,
                                    .min              = 0.0,
                                    .max              = 360.0,
                                    .on_value_changed = [&](std::optional<double> degrees) {
                                        // About the rectangle's center, clockwise.
                                        const ui::Size size = rect->actual_size();
                                        rect->set_center_point(ui::Vector3{size.width / 2.0f, size.height / 2.0f, 0.0f});
                                        rect->set_rotation(static_cast<float>(degrees.value_or(0.0)));
                                    },
                                },
                            },
                        }

                        // Once built: every set_rotation() turns there over 300 ms.
                        rect->set_rotation_transition(ui::ScalarTransition{});
                    )cpp",
                    .options = ui::VerticalStack{
                        .child_alignment = ui::Alignment::Stretch,
                        .width       = 240.0f,
                        .content =
                            {
                                ui::NumberBox{.header = "Rotation (0.0 to 360.0)",
                                              .value  = 45.0,
                                              .min    = 0.0,
                                              .max    = 360.0,
                                              .margin = ui::Thickness{5.0f},
                                              .on_value_changed = [apply_rotation](std::optional<double>) { apply_rotation(); },
                                              .control              = s.rotation_box},
                                ui::Button{.text     = "Set Rotation",
                                           .margin   = ui::Thickness{5.0f},
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click = [apply_rotation] { apply_rotation(); }},
                            }},
                    .controls = {std::move(gradient)},
                }),
                example({
                    .title    = "Automatically animate changes to Scale",
                    .code     = R"cpp(
                        ui::Control<ui::Rectangle> rect;
                        ui::Control<ui::CheckBox>  x, y, z;
                        const ui::Color            accent = window.theme().accent_ramp.base;

                        // The transition animates only the ticked components; the others snap.
                        const auto scale_to = [&](float value) {
                            ui::Vector3Components components = ui::Vector3Components::None;
                            if (x->checked() == true) components = components | ui::Vector3Components::X;
                            if (y->checked() == true) components = components | ui::Vector3Components::Y;
                            if (z->checked() == true) components = components | ui::Vector3Components::Z;
                            rect->set_scale_transition(ui::Vector3Transition{.components = components});
                            rect->set_scale(ui::Vector3{value, value, value});
                        };

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Rectangle{.fill = accent, .width = 50, .height = 50, .control = rect},
                                ui::Button{.text = "Set Scale to (0.5, 0.5, 0.5)", .on_click = [&] { scale_to(0.5f); }},
                                ui::Button{.text = "Set Scale to (2.0, 2.0, 2.0)", .on_click = [&] { scale_to(2.0f); }},
                                ui::CheckBox{.text = "Animate X", .checked = std::optional<bool>{true}, .control = x},
                                ui::CheckBox{.text = "Animate Y", .checked = std::optional<bool>{true}, .control = y},
                                ui::CheckBox{.text = "Animate Z", .checked = std::optional<bool>{true}, .control = z},
                            },
                        }
                    )cpp",
                    .options  = vector_options("Set Scale to ({:.1f}, {:.1f}, {:.1f})",
                                              {0.5f, 1.0f, 2.0f},
                                              {&s.scale_x, &s.scale_y, &s.scale_z},
                                              "Scale (0.0 to 5.0)",
                                              5.0,
                                              s.scale_box,
                                              "Set custom scale",
                                              apply_scale),
                    .controls = {std::move(scale)},
                }),
                example({
                    .title    = "Automatically animate changes to Translation",
                    .code     = R"cpp(
                        ui::Control<ui::Rectangle> rect;
                        ui::Control<ui::CheckBox>  x, y, z;
                        const ui::Color            accent = window.theme().accent_ramp.base;

                        // The transition animates only the ticked components; the others snap.
                        const auto move_to = [&](float value) {
                            ui::Vector3Components components = ui::Vector3Components::None;
                            if (x->checked() == true) components = components | ui::Vector3Components::X;
                            if (y->checked() == true) components = components | ui::Vector3Components::Y;
                            if (z->checked() == true) components = components | ui::Vector3Components::Z;
                            rect->set_translation_transition(ui::Vector3Transition{.components = components});
                            rect->set_translation(ui::Vector3{value, value, value});
                        };

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Rectangle{.fill = accent, .width = 50, .height = 50, .control = rect},
                                ui::Button{.text = "Set Translation to (0, 0, 0)", .on_click = [&] { move_to(0.0f); }},
                                ui::Button{.text = "Set Translation to (200, 200, 200)", .on_click = [&] { move_to(200.0f); }},
                                ui::CheckBox{.text = "Animate X", .checked = std::optional<bool>{true}, .control = x},
                                ui::CheckBox{.text = "Animate Y", .checked = std::optional<bool>{true}, .control = y},
                                ui::CheckBox{.text = "Animate Z", .checked = std::optional<bool>{true}, .control = z},
                            },
                        }
                    )cpp",
                    .options  = vector_options("Set Translation to ({:.0f}, {:.0f}, {:.0f})",
                                              {0.0f, 100.0f, 200.0f},
                                              {&s.translate_x, &s.translate_y, &s.translate_z},
                                              "Translation (0.0 to 200.0)",
                                              200.0,
                                              s.translation_box,
                                              "Set custom Translation",
                                              apply_translation),
                    .controls = {std::move(translate)},
                }),
                example({
                    .title    = "Implicitly animate  when the Background changes",
                    .code     = R"cpp(
                        const ui::Color         blue   = ui::Color::from_argb(0xFF0000FF);
                        const ui::Color         yellow = ui::Color::from_argb(0xFFFFFF00);
                        ui::Control<ui::Border> border;

                        ui::HorizontalStack{
                            .spacing = 12,
                            .content = {
                                ui::Border{
                                    .background = std::optional<ui::Color>{blue},
                                    .width      = 50,
                                    .height     = 50,
                                    .control    = border,
                                },
                                ui::Button{
                                    .text     = "Change Background Color",
                                    .on_click = [&] {
                                        const bool is_blue = border->background() == std::optional<ui::Color>{blue};
                                        border->set_background(is_blue ? yellow : blue);
                                    },
                                },
                            },
                        }

                        // Once built: a background change blends from the color on screen.
                        border->set_background_transition(ui::BrushTransition{});
                    )cpp",
                    .options  = ui::Button{.text     = "Change Background Color",
                                          .on_click =
                                              [&s]
                                              {
                                                  // A new brush each time; the transition blends between them.
                                                  const bool blue = s.brush_presenter->background() == std::optional<ui::Color>{Blue};
                                                  s.brush_presenter->set_background(blue ? Yellow : Blue);
                                              }},
                    .controls = {ui::Border{.background = std::optional<ui::Color>{Blue},
                                            .margin     = ui::Edges{.left = 45.0f, .top = 5.0f, .right = 5.0f, .bottom = 5.0f},
                                            .width      = 50.0f,
                                            .height     = 50.0f,
                                            .vertical_alignment    = ui::Alignment::Leading,
                                            .control        = s.brush_presenter}},
                }),
                example({
                    .title    = "Implicitly animate  when the Grid's theme changes",
                    .code     = R"cpp(
                        ui::Control<ui::Border> panel;

                        ui::HorizontalStack{
                            .spacing = 12,
                            .content = {
                                ui::Border{
                                    .border_thickness = ui::Thickness{1},
                                    .width            = 300,
                                    .min_height       = 200,
                                    .control          = panel,
                                    .content          = ui::VerticalStack{
                                        .spacing = 6,
                                        .margin  = ui::Thickness{12},
                                        .content = {
                                            ui::Text{.text = "Lorem Ipsum", .role = ui::TextRole::Subtitle},
                                            ui::Button{.text = "Action"},
                                            ui::CheckBox{.text = "Option"},
                                        },
                                    },
                                },
                                ui::Button{
                                    .text     = "Change Theme",
                                    .on_click = [&] {
                                        panel->set_requested_theme(panel->requested_theme() == ui::ElementTheme::Dark
                                                                       ? ui::ElementTheme::Light
                                                                       : ui::ElementTheme::Dark);
                                    },
                                },
                            },
                        }

                        // Once built: a background read from the theme, so a theme change is a
                        // background change, and the transition blends it.
                        panel->set_background([](const ui::Theme& t) { return t.colors().background; });
                        panel->set_background_transition(ui::BrushTransition{});
                        panel->set_requested_theme(ui::ElementTheme::Light);
                    )cpp",
                    .options  = ui::Button{.text     = "Change Theme",
                                          .vertical_alignment  = ui::Alignment::Leading,
                                          .on_click =
                                              [&s]
                                              {
                                                  s.theme_grid->set_requested_theme(
                                                      s.theme_grid->requested_theme() == ui::ElementTheme::Dark ? ui::ElementTheme::Light
                                                                                                            : ui::ElementTheme::Dark);
                                              }},
                    .controls = {ui::Border{
                        .border_brush     = std::optional<ui::Color>{DimGray},
                        .border_thickness = ui::Thickness{1.0f},
                        .width            = 300.0f,
                        .min_height       = 200.0f,
                        .vertical_alignment          = ui::Alignment::Leading,
                        .control              = s.theme_grid,
                        .content = ui::VerticalStack{
                            .spacing = 6.0f,
                            .margin  = ui::Thickness{12.0f},
                            .content =
                                {
                                    ui::Text{.text = "Lorem Ipsum", .role = ui::TextRole::Subtitle, .horizontal_alignment = ui::Alignment::Leading},
                                    paragraph("The background of this grid animates when the theme changes.", ui::Thickness{}),
                                    ui::Button{.text = "Action", .horizontal_alignment = ui::Alignment::Leading},
                                    ui::CheckBox{.text = "Option"},
                                }}}},
                }),
            }));

            // The implicit transitions on each rectangle.
            s.opacity_rect->set_opacity(0.5f);
            s.opacity_rect->set_opacity_transition(ui::ScalarTransition{});
            s.rotation_rect->set_rotation_transition(ui::ScalarTransition{});
            s.scale_rect->set_scale_transition(ui::Vector3Transition{});
            s.translate_rect->set_translation_transition(ui::Vector3Transition{});
            s.brush_presenter->set_background_transition(ui::BrushTransition{});
            s.theme_grid->set_background([](const ui::Theme& t) { return t.colors().background; });
            s.theme_grid->set_background_transition(ui::BrushTransition{});
            s.theme_grid->set_requested_theme(ui::ElementTheme::Light);
            return ui::Spec{std::move(page)};
        });
}

} // namespace gallery

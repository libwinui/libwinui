// SPDX-License-Identifier: MIT
// The Motion pages' shared bits. Private to the pages_motion*.cpp files, one page or two each.
#pragma once

#include "gallery.hpp"

#include <algorithm>
#include <memory>
#include <optional>

#include <cmath>

namespace gallery
{
// ---- shared bits ----

// The named colors the samples paint with.
inline constexpr ui::Color LightBlue  = ui::Color::from_argb(0xFFADD8E6);
inline constexpr ui::Color LightGray  = ui::Color::from_argb(0xFFD3D3D3);
inline constexpr ui::Color Red        = ui::Color::from_argb(0xFFFF0000);
inline constexpr ui::Color Green      = ui::Color::from_argb(0xFF008000);
inline constexpr ui::Color Blue       = ui::Color::from_argb(0xFF0000FF);
inline constexpr ui::Color Yellow     = ui::Color::from_argb(0xFFFFFF00);
inline constexpr ui::Color DimGray    = ui::Color::from_argb(0xFF696969);
inline constexpr ui::Color ChromeGray = ui::Color::from_argb(0xFF767676); // a mid gray

inline constexpr const char* Lorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore "
    "magna "
    "aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
    "consequat. Duis "
    "aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
    "occaecat "
    "cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

// The theme's accent color.
inline ui::Color system_accent(const ui::Theme& t)
{
    return t.accent_ramp.base;
}
// The theme's flyout background.
inline ui::Color flyout_background(const ui::Theme& t)
{
    return t.surfaces.menu;
}
// The mid gray of the motion samples, the same in every theme.
inline ui::Color chrome_gray(const ui::Theme&)
{
    return ChromeGray;
}

inline ui::Spec options_panel(float width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// A paragraph in the body style, wrapping.
inline ui::Spec paragraph(const char* text, ui::Thickness margin = ui::Edges{.bottom = 12.0f})
{
    return ui::Text{.text                 = text,
                    .wrapping             = ui::TextWrapping::WrapWholeWords,
                    .margin               = margin,
                    .horizontal_alignment = ui::Alignment::Leading};
}

// A Rectangle or Ellipse filled with a theme color (the accent, say),
// re-read from the theme it is drawn in. A gradient top fades from that color down to the resource.
class ThemedShape final : public ui::Element
{
public:
    enum class Kind
    {
        Rectangle,
        Ellipse,
    };

    ThemedShape(Kind kind, ui::ThemeColor fill, float width, float height) : kind_(kind), fill_(fill)
    {
        set_width(width);
        set_height(height);
    }

    void set_stroke(ui::ThemeColor stroke, float thickness)
    {
        stroke_           = stroke;
        stroke_thickness_ = thickness;
    }
    void set_gradient_top(ui::Color top)
    {
        gradient_top_ = top;
    }

protected:
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{};
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        const ui::Color  fill  = fill_(theme);
        if (kind_ == Kind::Ellipse)
        {
            const float half = stroke_ != nullptr ? stroke_thickness_ * 0.5f : 0.0f;
            const float rx   = std::max(0.0f, bounds().width * 0.5f - half);
            const float ry   = std::max(0.0f, bounds().height * 0.5f - half);
            renderer.fill_ellipse(bounds().center(), rx, ry, fill);
            if (stroke_ != nullptr)
            {
                renderer.stroke_ellipse(bounds().center(), rx, ry, stroke_(theme), stroke_thickness_);
            }
            return;
        }
        if (gradient_top_)
        {
            renderer.fill_rounded_rect_gradient(bounds(),
                                                0.0f,
                                                ui::LinearGradientBrush{{ui::GradientStop{0.0f, *gradient_top_},
                                                                         ui::GradientStop{1.0f, fill}},
                                                                        ui::GradientAxis::Vertical});
            return;
        }
        renderer.fill_rounded_rect(bounds(), 0.0f, fill);
    }


private:
    Kind                     kind_;
    ui::ThemeColor           fill_;
    ui::ThemeColor           stroke_           = nullptr;
    float                    stroke_thickness_ = 0.0f;
    std::optional<ui::Color> gradient_top_{};
};

// A square Rectangle filled with the accent color.
inline std::unique_ptr<ThemedShape> accent_rectangle(float size, ThemedShape** out, ui::Thickness margin = ui::Thickness{})
{
    auto shape = std::make_unique<ThemedShape>(ThemedShape::Kind::Rectangle, system_accent, size, size);
    shape->set_margin(margin);
    *out = shape.get();
    return shape;
}

inline ui::Spec rectangle(ui::Color                   fill,
                          float                       width,
                          float                       height,
                          ui::Thickness               margin = ui::Thickness{},
                          ui::Control<ui::Rectangle>* ref    = nullptr)
{
    return ui::Rectangle{.fill = fill, .margin = margin, .width = width, .height = height, .control = ref};
}

// A NumberBox's value, where an empty box becomes 0, and
// shows it.
inline float value_or_zero(ui::NumberBoxControl* box)
{
    if (!box->value())
    {
        box->set_value(0.0);
    }
    return static_cast<float>(box->value().value_or(0.0));
}

// ---- the pages, in navigation order ----

ui::Spec animation_interop_page(Context& ctx);
ui::Spec connected_animation_page(Context& ctx);
ui::Spec easing_functions_page(Context& ctx);
ui::Spec implicit_transitions_page(Context& ctx);
ui::Spec page_transitions_page(Context& ctx);
ui::Spec element_transitions_page(Context& ctx);
ui::Spec parallax_view_page(Context& ctx);

} // namespace gallery

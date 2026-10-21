// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// One child inside a background, a border and padding.
///
/// Measures to the child's size plus border thickness and padding on each side, and arranges
/// the child inside both. The background fills the rect inside the border, and the border is
/// drawn inside the element's own bounds, both with corner_radius(). Input, focus, ticks and
/// popups pass through to the child, so wrapping a subtree in a Border changes only its look and
/// its size. Set the child with ContentHost::set_child().
///
/// Example:
/// @code
/// auto border = std::make_unique<ui::BorderControl>();
/// border->set_background(ui::colors::White);
/// border->set_border_brush(ui::Color::from_rgba(255, 215, 0));
/// border->set_border_thickness(ui::Thickness{2.0f});
/// border->set_corner_radius(4.0f);
/// border->set_padding(ui::Thickness{12.0f});
/// ui::TextControl* label = border->set_child(std::make_unique<ui::TextControl>("Inside a border"));
/// window.set_content(std::move(border));
///
/// // Declaratively:
/// window.set_content(ui::Border{
///     .background       = ui::colors::White,
///     .border_brush     = ui::Color::from_rgba(255, 215, 0),
///     .border_thickness = ui::Thickness{2.0f},
///     .corner_radius    = 4.0f,
///     .padding          = ui::Thickness{12.0f},
///     .content          = ui::Text{.text = "Inside a border"},
/// });
/// @endcode
class BorderControl final : public ContentHost
{
public:
    /// Returns false, so the border is hit anywhere in its bounds, even where it draws nothing.
    [[nodiscard]] bool hit_test_transparent() const override
    {
        return false;
    }
    /// An empty border: no child, no background, no border, no padding.
    BorderControl() = default;

    /// The fixed background color. nullopt, the default, draws nothing. It also reads nullopt while
    /// a theme resource is set (see background_resource()). A ThemeColor is read again whenever the
    /// border is drawn, so it follows theme switches.
    [[nodiscard]] const std::optional<Color>& background() const
    {
        return background_.color();
    }
    /// The theme resource set as the background, or nullptr when a fixed color or none is set.
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_.resource();
    }
    /// Sets a fixed background color, or none with nullopt, and clears any theme resource.
    /// Redraws.
    void set_background(std::optional<Color> color)
    {
        background_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// Sets a theme resource as the background, for example
    /// `[](const Theme& t) { return t.colors().background; }`. Redraws.
    void set_background(ThemeColor resource)
    {
        background_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }
    /// How the background changes color. With a BrushTransition set, a later change of what the
    /// background draws, such as a new color or a resource under a new theme, blends from the color
    /// on screen. nullopt, the default, changes it at once.
    [[nodiscard]] const std::optional<BrushTransition>& background_transition() const
    {
        return background_.transition();
    }
    /// Sets the background transition. Applies to the next change, and a blend already running
    /// keeps going.
    void set_background_transition(std::optional<BrushTransition> transition)
    {
        background_.set_transition(transition);
    }

    /// The fixed border color. nullopt, the default, draws no border, though border_thickness()
    /// still takes up room.
    [[nodiscard]] const std::optional<Color>& border_brush() const
    {
        return border_brush_;
    }
    /// The theme resource set as the border brush, or nullptr.
    [[nodiscard]] ThemeColor border_brush_resource() const
    {
        return border_brush_resource_;
    }
    /// Sets a fixed border color and clears any theme resource set with the other overload.
    /// Redraws.
    void set_border_brush(std::optional<Color> color)
    {
        border_brush_          = color;
        border_brush_resource_ = nullptr;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Sets a theme resource as the border brush, read again under every theme. It takes
    /// precedence over a fixed border_brush(). Redraws.
    void set_border_brush(ThemeColor resource)
    {
        border_brush_resource_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The border width per side in DIPs, zero by default. It takes up room whether or not a
    /// border brush is set.
    [[nodiscard]] const Thickness& border_thickness() const
    {
        return border_thickness_;
    }
    /// Sets the border width. Updates layout and redraws.
    void set_border_thickness(const Thickness& thickness)
    {
        border_thickness_ = thickness;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The corner radius in DIPs, one value for all four corners. 0, the default, gives square
    /// corners. Applies to the background and the border alike.
    [[nodiscard]] float corner_radius() const
    {
        return corner_radius_;
    }
    /// Sets the corner radius. Redraws only, since layout doesn't depend on the radius.
    void set_corner_radius(float radius)
    {
        corner_radius_ = radius;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The padding in DIPs between the inside of the border and the child, zero by default.
    [[nodiscard]] const Thickness& padding() const
    {
        return padding_;
    }
    /// Sets the padding. Updates layout.
    void set_padding(const Thickness& padding)
    {
        padding_ = padding;
        mark_dirty(DirtyFlags::Layout);
    }

    /// Whether the border casts a drop shadow sized by elevation(). It draws two black
    /// shadows, ambient and directional, whose blur, offset and opacity grow with the depth and
    /// differ per theme. Off by default.
    [[nodiscard]] bool shadow() const
    {
        return shadow_;
    }
    /// Turns the shadow on or off. Redraws only, since the shadow takes no layout room.
    void set_shadow(bool on)
    {
        shadow_ = on;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The elevation the shadow is sized by, in DIPs. 0 by default, and below 4 casts no
    /// shadow. It moves nothing on screen. It is the same value as Element::translation().z, so a
    /// translation animation or transition deepens the shadow as it runs.
    [[nodiscard]] float elevation() const
    {
        return translation().z;
    }
    /// Sets only the z of Element::translation(), keeping x and y.
    void set_elevation(float z)
    {
        Vector3 t = translation();
        t.z       = z;
        set_translation(t);
    }

    /// Whether the child is clipped to the inside of the border, rounded corners included. Off by
    /// default. When on, a picture in a rounded Border follows the corners, and a child drawn past
    /// the edge, through a negative margin or a transform, is cut off there.
    [[nodiscard]] bool clip_content() const
    {
        return clip_content_;
    }
    /// Turns clipping on or off. Redraws.
    void set_clip_content(bool clip)
    {
        clip_content_ = clip;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Returns true, since measuring touches only the child.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    /// The child's desired size plus border thickness and padding on each side.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Gives the child the rect inside the border and the padding.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the shadow if it is on, the background and the border, then the child. The child is
    /// clipped to the inside of the border while clip_content() is on.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the background transition, then forwards to the child.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] Thickness combined() const;

    BackgroundBrush      background_;
    std::optional<Color> border_brush_;
    ThemeColor           border_brush_resource_ = nullptr;
    Thickness            border_thickness_{};
    float                corner_radius_ = 0.0f;
    Thickness            padding_{};
    bool                 shadow_       = false;
    bool                 clip_content_ = false;
};

/// A background, a border and padding around one child. Builds a ui::BorderControl.
///
/// Example:
/// @code
/// const ui::Color gold = ui::Color::from_rgba(255, 215, 0);
///
/// window.set_content(ui::Border{
///     .background           = ui::colors::White,
///     .border_brush         = gold,
///     .border_thickness     = ui::Thickness{2.0f},
///     .corner_radius        = 4.0f,
///     .padding              = ui::Thickness{12.0f},
///     .horizontal_alignment = ui::Alignment::Leading,
///     .content              = ui::Text{.text = "Text inside a border"},
/// });
/// @endcode
struct Border
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Border>` points at.
    using control_type = BorderControl;

    // ---- properties ----
    /// The background inside the border: a Color, or a ThemeColor read from the theme. Empty, the
    /// default, draws none.
    Binding<Paint> background{};
    /// The border color, a Color or a ThemeColor. Empty, the default, draws no border.
    Binding<Paint> border_brush{};
    /// The border width per edge, DIPs.
    Binding<Thickness> border_thickness{Thickness{}};
    /// The corner radius in DIPs, for the background and border alike.
    Binding<float> corner_radius{0.0f};
    /// The padding between the border and the child, DIPs.
    Binding<Thickness> padding{Thickness{}};
    /// Whether the border casts a drop shadow sized by `elevation`.
    Binding<bool> shadow{false};
    /// The elevation in DIPs that sizes the `shadow`.
    Binding<float> elevation{0.0f};
    /// Whether the child is clipped to the inside of the border, rounded corners included, so a
    /// picture follows the corners. Off by default, which lets a child draw past the edge.
    Binding<bool> clip_content{false};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Border> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The framed child.
    Spec content{};

    /// Consumes the spec and returns the built ui::BorderControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

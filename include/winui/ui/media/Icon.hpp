// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// A non-interactive icon: a symbol, a font glyph, a path, a bitmap or a picture.
///
/// It takes no input and no focus. Use it on its own or as part of a control's content, for
/// example an icon beside a label in a Button. A glyph is drawn at its own size in the theme's
/// symbol font, which is 20 DIPs for a Symbol. A path, a bitmap or a picture measures to its
/// natural size and is fitted into the icon's box with its aspect ratio kept. size() makes any of
/// them a square of that many DIPs. A glyph, a path and a monochrome bitmap take the theme's text
/// color unless set_foreground() overrides it. A picture keeps its own colors. The declarative
/// form is `ui::Icon`.
///
/// Example:
/// @code
/// // Declarative: an icon-only button.
/// window.set_content(ui::Button{.content = ui::Icon{.source = ui::Symbol::Accept}});
///
/// // Imperative: icon + text as a Button's content.
/// auto button  = std::make_unique<ui::ButtonControl>();
/// auto content = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// content->set_spacing(8.0f);
/// content->add(std::make_unique<ui::IconControl>(ui::Symbol::Save));
/// content->add(std::make_unique<ui::TextControl>("Save"));
/// button->set_content(std::move(content));
/// @endcode
class IconControl final : public Element
{
public:
    /// An icon with no source. It measures to nothing and draws nothing.
    IconControl() = default;
    /// An icon showing `source`.
    explicit IconControl(IconSource source) : source_(std::move(source))
    {
    }

    /// The icon drawn. Empty by default.
    [[nodiscard]] const IconSource& source() const
    {
        return source_;
    }
    /// Replaces the icon. Updates layout and redraws.
    void set_source(IconSource source)
    {
        source_ = std::move(source);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The icon's size as a square of DIPs: a glyph's font size, or the square a path or picture is
    /// fitted into. nullopt (the default) is the glyph's own size, or the natural size of anything
    /// else.
    [[nodiscard]] std::optional<float> size() const
    {
        return size_;
    }
    /// Sets or clears the size. Updates layout and redraws.
    void set_size(OptionalFloat size)
    {
        size_ = size;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The color of a glyph, a path or a monochrome bitmap: a fixed color or a theme resource.
    /// Unset (the default) is the theme's text color.
    [[nodiscard]] const Paint& foreground() const
    {
        return foreground_;
    }
    /// Draws in `color` instead of the theme's text color. An empty Paint goes back to the theme's
    /// color. Redraws.
    void set_foreground(Paint color)
    {
        foreground_ = color;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Returns true, since measuring reads only the source's size.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- automation ----
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Image;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    IconSource           source_{};
    std::optional<float> size_{};
    Paint                foreground_{};
};

/// An icon: a symbol, a font glyph, a path, a bitmap or a picture. Builds a ui::IconControl.
///
/// An icon carries no text, so give it an accessible name. Set `.accessibility.decorative`
/// instead when a label next to it already says the same thing.
///
/// Example:
/// @code
/// window.set_content(ui::HorizontalStack{
///     .spacing = 8,
///     .content = {
///         ui::Icon{ .source = ui::Symbol::Save, .accessibility = { .name = "Save" } },
///         ui::Icon{ .source = ui::IconGlyph{0xE77B}, .size = 16 },
///         ui::Icon{ .source = ui::IconSource::path(*ui::PathGeometry::parse("M 0,0 L 12,6 0,12 Z")) },
///         ui::Icon{ .source = ui::Bitmap::read("logo.png"), .size = 32 },
///     },
/// });
/// @endcode
struct Icon
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Icon>` points at.
    using control_type = IconControl;

    // ---- properties ----
    /// The icon: a Symbol, an IconGlyph, an ImageSource, or IconSource::path()/bitmap().
    Binding<IconSource> source{};
    /// The icon's size as a square of DIPs; unset is the glyph's own size or the natural size.
    Binding<std::optional<float>> size{};
    /// The color of a glyph, a path or a monochrome bitmap: a fixed color or a theme resource.
    /// Unset uses the theme's text color.
    Binding<Paint> foreground{};

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
    ControlOut<Icon> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::IconControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ui
{

/// Which way a ScaleToFit may scale its child.
enum class StretchDirection
{
    UpOnly,   ///< Only enlarge. A child bigger than the room is drawn at its natural size.
    DownOnly, ///< Only shrink. A child smaller than the room is drawn at its natural size.
    Both,     ///< The default. Scale either way.
};

/// One child, laid out at its natural size and drawn scaled to the room the ScaleToFit is given.
///
/// The child is measured with unbounded room, then drawn with a scale that stretch() and
/// stretch_direction() decide. The ScaleToFit asks for the scaled size. Text is scaled like a
/// picture and keeps its line breaks and font size. Pointer input is mapped back through the same
/// scale, so the child's controls keep working at any size. A popup opened from inside is anchored
/// where its owner would be unscaled. With UniformToFill, the overflow is clipped.
///
/// Example:
/// @code
/// auto  box   = std::make_unique<ui::ScaleToFitControl>();
/// auto* label = box->set_child(std::make_unique<ui::TextControl>("42"));
/// box->set_stretch(ui::Stretch::Uniform);
/// box->set_stretch_direction(ui::StretchDirection::UpOnly); // grow the text, never shrink it
/// window.set_content(std::move(box));
/// label->set_text("43"); // the observer stays valid, and the next layout scales the new text
///
/// // Or declaratively, filling a 200x100 slot:
/// window.set_content(ui::ScaleToFit{
///     .width   = 200.0f,
///     .height  = 100.0f,
///     .content = ui::Text{.text = "42"},
/// });
/// @endcode
class ScaleToFitControl final : public Element
{
public:
    /// An empty ScaleToFit, Uniform and Both.
    ScaleToFitControl() = default;

    /// Takes ownership of the single child and returns a non-owning observer. Replaces and
    /// destroys any previous child, and nullptr clears it. Updates layout.
    template <typename T>
    T* set_child(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "ScaleToFit child must be an Element");
        T* observer = child.get();
        if (observer)
        {
            set_parent_of(*observer, this);
        }
        child_ = std::move(child);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The child, or nullptr. Owned by the ScaleToFit.
    [[nodiscard]] Element* child() const
    {
        return child_.get();
    }

    /// How the child is scaled. `Uniform` by default. None never scales, and Fill scales each axis
    /// to its room. Uniform fits inside the room and UniformToFill covers it, both keeping the
    /// aspect ratio.
    [[nodiscard]] Stretch stretch() const
    {
        return stretch_;
    }
    /// Changes the stretch. Updates layout.
    void set_stretch(Stretch stretch)
    {
        stretch_ = stretch;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether scaling may enlarge, shrink or both. `Both` by default.
    [[nodiscard]] StretchDirection stretch_direction() const
    {
        return stretch_direction_;
    }
    /// Changes which way scaling may go. Updates layout.
    void set_stretch_direction(StretchDirection direction)
    {
        stretch_direction_ = direction;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The horizontal scale the child is drawn at, from the last arrange. 1 before the first.
    [[nodiscard]] float scale_x() const
    {
        return scale_x_;
    }
    /// The vertical scale, like scale_x(). The two differ only under Stretch::Fill.
    [[nodiscard]] float scale_y() const
    {
        return scale_y_;
    }

    /// The x and y factors that fit `content` into `available` under the current stretch() and
    /// stretch_direction(). An unbounded axis (ui::Unbounded) uses the other axis's factor. With
    /// both unbounded, the factors are 1 and 1.
    [[nodiscard]] Size compute_scale_factor(Size available, Size content) const;
    /// The child, when there is one.
    void for_each_child(ChildVisitor visit) const override
    {
        if (child_)
        {
            visit(*child_);
        }
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    /// Maps a screen point into the child's own unscaled layout space.
    [[nodiscard]] Point to_child(Point p) const;

protected:
    /// The point as the scaled child sees it.
    [[nodiscard]] Point map_to_children(Point p) const override
    {
        return to_child(p);
    }
    /// The child is clipped to the bounds, and can be hit only inside them.
    [[nodiscard]] std::optional<Rect> children_hit_clip() const override
    {
        return bounds();
    }

private:
    std::unique_ptr<Element> child_;
    Stretch                  stretch_           = Stretch::Uniform;
    StretchDirection         stretch_direction_ = StretchDirection::Both;

    mutable Size child_desired_{};
    float        scale_x_ = 1.0f;
    float        scale_y_ = 1.0f;
    Point        offset_{}; // screen = child * scale + offset_
};

/// One child scaled to the room it is given. Builds a ui::ScaleToFitControl.
///
/// The child is measured at its natural size and then scaled, text and strokes included.
///
/// Example:
/// @code
/// window.set_content(ui::ScaleToFit{
///     .stretch           = ui::Stretch::Uniform,
///     .stretch_direction = ui::StretchDirection::DownOnly, // shrink to fit, never enlarge
///     .width             = 200.0f,
///     .height            = 100.0f,
///     .content           = ui::Text{.text = "Scaled to fit", .role = ui::TextRole::Header},
/// });
/// @endcode
struct ScaleToFit
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ScaleToFit>` points at.
    using control_type = ScaleToFitControl;

    // ---- properties ----
    /// How the child's natural size maps to the box. Uniform, the default, keeps its aspect ratio.
    Binding<Stretch> stretch{Stretch::Uniform};
    /// UpOnly only enlarges, DownOnly only shrinks, and Both, the default, does both.
    Binding<StretchDirection> stretch_direction{StretchDirection::Both};

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
    ControlOut<ScaleToFit> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The scaled child.
    Spec content{};

    /// Consumes the spec and returns the built ui::ScaleToFitControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

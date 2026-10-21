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

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace ui
{

/// Whether a Parallax source start or end offset is added to the automatic value (Relative, the
/// default) or used as it is (Absolute).
enum class ParallaxSourceOffsetKind
{
    Absolute, ///< The offset is the scroll position itself, in unzoomed DIPs.
    Relative, ///< The offset is added to the automatically computed start/end.
};

/// Moves its single child at a different rate than another element scrolls, typically a
/// background image behind a list or scroll view.
///
/// The child is laid out larger than the view by the shift, clipped to the view, and moved
/// every frame through its own translation, which the Parallax controls from then on. The
/// source is any element with `Element::scroll_info()`, such as a `Scroll` or a `ListView`. The
/// Parallax does not own it and notices when it is destroyed. Set it after the tree is built.
/// A source destroyed first stops the parallax. A Parallax is never focusable. `ui::Parallax` is
/// the declarative form. It has no `.source` field, so set the source through `.control`.
///
/// Example:
/// @code
/// auto row = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
///
/// auto* parallax = row->add(std::make_unique<ui::ParallaxControl>());
/// parallax->set_vertical_shift(300.0f); // the background travels 300 DIPs over the whole scroll
/// parallax->set_child(std::make_unique<ui::TextControl>("Background"));
///
/// auto* scroll  = row->add(std::make_unique<ui::ScrollControl>());
/// auto  content = std::make_unique<ui::TextControl>("Tall content");
/// content->set_min_height(2000.0f);
/// scroll->set_child(std::move(content));
///
/// parallax->set_source(scroll); // non-owning; both already live in the same tree
/// window.set_content(std::move(row));
/// @endcode
class ParallaxControl final : public Element
{
public:
    /// An empty view with no child, no source and zero shift on both axes.
    ParallaxControl() = default;

    // ---- content ----
    /// Takes ownership of the moving content and returns a non-owning pointer to it. The
    /// Parallax controls the child's translation from then on.
    template <typename T>
    T* set_child(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "Parallax content must be an Element");
        T* observer = child.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        child_ = std::move(child);
        if (child_)
        {
            attach_child();
        }
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The content set by `set_child()` (non-owning), or nullptr.
    [[nodiscard]] Element* child() const
    {
        return child_.get();
    }

    // ---- source ----
    /// The scrolling element to follow (Element::scroll_info()), or nullptr for none. Becomes
    /// nullptr by itself once the source is destroyed.
    [[nodiscard]] Element* source() const
    {
        return source_.get();
    }
    /// Follows `source`, which the Parallax does not own. Pass nullptr to stop following.
    /// Redraws.
    void set_source(Element* source)
    {
        source_.reset(source);
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- shift ----
    /// How far the child moves over the source's horizontal range, in DIPs. 0, the default, is
    /// no parallax on that axis. The child is laid out this much larger than the view on that
    /// axis.
    [[nodiscard]] float horizontal_shift() const
    {
        return horizontal_.shift;
    }
    /// Lays the view out again, since the child's size depends on the shift, and redraws.
    void set_horizontal_shift(float shift)
    {
        horizontal_.shift = shift;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Vertical counterpart of `horizontal_shift()`.
    [[nodiscard]] float vertical_shift() const
    {
        return vertical_.shift;
    }
    /// Lays the view out again and redraws.
    void set_vertical_shift(float shift)
    {
        vertical_.shift = shift;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Where the horizontal parallax starts on the source's scroll range, in DIPs. It is added to
    /// the automatic value or used as is, depending on the offset kind. 0 by default, as are the
    /// other offsets.
    [[nodiscard]] float horizontal_source_start_offset() const
    {
        return horizontal_.start;
    }
    /// Redraws. The shift is recomputed on the next frame.
    void set_horizontal_source_start_offset(float offset)
    {
        horizontal_.start = offset;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Where the horizontal parallax ends on the source's scroll range (see the start offset).
    [[nodiscard]] float horizontal_source_end_offset() const
    {
        return horizontal_.end;
    }
    /// Marks the view for redraw.
    void set_horizontal_source_end_offset(float offset)
    {
        horizontal_.end = offset;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Where the vertical parallax starts on the source's scroll range, in DIPs.
    [[nodiscard]] float vertical_source_start_offset() const
    {
        return vertical_.start;
    }
    /// Marks the view for redraw.
    void set_vertical_source_start_offset(float offset)
    {
        vertical_.start = offset;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Where the vertical parallax ends on the source's scroll range, in DIPs.
    [[nodiscard]] float vertical_source_end_offset() const
    {
        return vertical_.end;
    }
    /// Marks the view for redraw.
    void set_vertical_source_end_offset(float offset)
    {
        vertical_.end = offset;
        mark_dirty(DirtyFlags::Visual);
    }

    /// How the horizontal start/end offsets are interpreted; Relative by default.
    [[nodiscard]] ParallaxSourceOffsetKind horizontal_source_offset_kind() const
    {
        return horizontal_.kind;
    }
    /// Marks the view for redraw.
    void set_horizontal_source_offset_kind(ParallaxSourceOffsetKind kind)
    {
        horizontal_.kind = kind;
        mark_dirty(DirtyFlags::Visual);
    }
    /// How the vertical start/end offsets are interpreted; Relative by default.
    [[nodiscard]] ParallaxSourceOffsetKind vertical_source_offset_kind() const
    {
        return vertical_.kind;
    }
    /// Marks the view for redraw.
    void set_vertical_source_offset_kind(ParallaxSourceOffsetKind kind)
    {
        vertical_.kind = kind;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The child never moves faster than this times the source's own horizontal scrolling. 1.0
    /// by default.
    [[nodiscard]] float max_horizontal_shift_ratio() const
    {
        return horizontal_.max_ratio;
    }
    /// Marks the view for redraw.
    void set_max_horizontal_shift_ratio(float ratio)
    {
        horizontal_.max_ratio = ratio;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Vertical counterpart of `max_horizontal_shift_ratio()`; 1.0 by default.
    [[nodiscard]] float max_vertical_shift_ratio() const
    {
        return vertical_.max_ratio;
    }
    /// Marks the view for redraw.
    void set_max_vertical_shift_ratio(float ratio)
    {
        vertical_.max_ratio = ratio;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether the child stops at the ends of the horizontal range instead of moving on with the
    /// scroll. True by default.
    [[nodiscard]] bool is_horizontal_shift_clamped() const
    {
        return horizontal_.clamped;
    }
    /// Marks the view for redraw.
    void set_horizontal_shift_clamped(bool clamped)
    {
        horizontal_.clamped = clamped;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Vertical counterpart of `is_horizontal_shift_clamped()`; true by default.
    [[nodiscard]] bool is_vertical_shift_clamped() const
    {
        return vertical_.clamped;
    }
    /// Marks the view for redraw.
    void set_vertical_shift_clamped(bool clamped)
    {
        vertical_.clamped = clamped;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The child's current parallax offset on each axis, its translation.
    [[nodiscard]] Point current_shift() const;
    /// Visits the child, if there is one.
    void for_each_child(ChildVisitor visit) const override
    {
        if (child_)
        {
            visit(*child_);
        }
    }

protected:
    // ---- Element ----
    /// The child is clipped to the bounds, and so is where it can be hit.
    [[nodiscard]] std::optional<Rect> children_hit_clip() const override
    {
        return bounds();
    }
    /// Measures the child with the shift added to the available size. The Parallax takes the
    /// available size, or the child's size on an unbounded axis.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Arranges the child larger than the view by the shift. A child that stretches grows by the
    /// same ratio on the other axis.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the child clipped to the view. Its translation carries the shift.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    struct Axis
    {
        float                    shift     = 0.0f;
        float                    start     = 0.0f;
        float                    end       = 0.0f;
        ParallaxSourceOffsetKind kind      = ParallaxSourceOffsetKind::Relative;
        float                    max_ratio = 1.0f;
        bool                     clamped   = true;
    };

    void                attach_child();
    [[nodiscard]] float axis_shift(const Axis& axis, bool horizontal) const;

    std::unique_ptr<Element> child_;
    ElementWatch             source_;
    Axis                     horizontal_{};
    Axis                     vertical_{};
    mutable Size             child_desired_{};
};

/// Moves one child at a different rate than another element scrolls. Builds a
/// ui::ParallaxControl. There is no `.source` field. Call `set_source()` on `.control` once
/// both the view and its source exist.
///
/// Example:
/// @code
/// const std::vector<std::string>         titles{"Arches", "Bryce Canyon", "Zion"};
/// ui::Control<ui::Parallax>              parallax;
/// ui::Control<ui::ListView<std::string>> list;
///
/// window.set_content(ui::Grid{
///     .height  = 700.0f,
///     .content = {
///         ui::Cell{.content = ui::Parallax{.vertical_shift     = 500.0f,
///                                          .vertical_alignment = ui::Alignment::Leading,
///                                          .control            = parallax,
///                                          .content            = ui::Image{.source = ui::Bitmap::read("cliff.jpg")}}},
///         ui::Cell{.content = ui::ListView<std::string>{.items = titles, .control = list}},
///     },
/// });
/// parallax->set_source(list); // follow the list's scrolling
/// @endcode
struct Parallax
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Parallax>` points at.
    using control_type = ParallaxControl;

    // ---- properties ----
    /// How far the child moves over the source's horizontal scroll range, in DIPs. 0, the
    /// default, is no parallax on that axis. The child is laid out this much wider.
    Binding<float> horizontal_shift{0.0f};
    /// The vertical counterpart of `horizontal_shift`.
    Binding<float> vertical_shift{0.0f};
    /// Where on the source's horizontal scroll range the parallax starts and ends, in DIPs, read
    /// per `horizontal_source_offset_kind`.
    Binding<float> horizontal_source_start_offset{0.0f};
    Binding<float> horizontal_source_end_offset{0.0f}; ///< See above.
    /// Where on the source's vertical scroll range the parallax starts and ends, as the
    /// horizontal pair.
    Binding<float> vertical_source_start_offset{0.0f};
    Binding<float> vertical_source_end_offset{0.0f}; ///< See above.
    /// Relative (the default) adds the offsets to the automatic range; Absolute uses them as is.
    Binding<ParallaxSourceOffsetKind> horizontal_source_offset_kind{ParallaxSourceOffsetKind::Relative};
    /// As `horizontal_source_offset_kind`, for the vertical offsets.
    Binding<ParallaxSourceOffsetKind> vertical_source_offset_kind{ParallaxSourceOffsetKind::Relative};
    /// The child never moves faster than this times the source.
    Binding<float> max_horizontal_shift_ratio{1.0f};
    /// As `max_horizontal_shift_ratio`.
    Binding<float> max_vertical_shift_ratio{1.0f};
    /// The child stops at the ends of the range.
    bool horizontal_shift_clamped = true;
    /// As `horizontal_shift_clamped`.
    bool vertical_shift_clamped = true;

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
    ControlOut<Parallax> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The moving child, typically a background image.
    Spec content{};

    /// Consumes the spec and returns the built ui::ParallaxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

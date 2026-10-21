// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

class ToolTipSurface; // defined in ToolTip.cpp

/// Hover text for an element, opened after the pointer rests on the anchor and closed when the
/// pointer leaves or clicks.
///
/// A ToolTip wraps its anchor. It takes the anchor's place in the tree and passes layout,
/// rendering, input, focus and automation through, so the anchor behaves as it would unwrapped.
/// The tip content lives outside the main tree. The window lays it out and draws it on the
/// theme's tooltip surface. The open delay counts down in tick_override(), so the host must tick
/// the Window, as the built-in window host does. A tip opened by the mouse docks to the
/// pointer, inflated by the offsets. For plain text on any element, Element::set_tool_tip() is
/// shorter.
///
/// Example:
/// @code
/// auto  toolbar = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// auto  tip     = std::make_unique<ui::ToolTipControl>();
/// auto* save    = tip->set_anchor(std::make_unique<ui::ButtonControl>("Save"));
/// tip->set_content(std::make_unique<ui::TextControl>("Saves the current document"));
/// tip->set_placement(ui::PopupPlacement::Below);
/// save->on_click([] { save_document(); });
/// toolbar->add(std::move(tip)); // the ToolTip takes the Button's place in the tree
///
/// // Declaratively:
/// toolbar->add(ui::ToolTip{
///     .anchor  = ui::Button{.text = "Save"},
///     .content = ui::Text{.text = "Saves the current document"},
/// }.build());
/// @endcode
class ToolTipControl final : public Element
{
public:
    /// An empty tooltip: no anchor, no content, 800 ms delay, placed Above.
    ToolTipControl() = default;

    /// Takes ownership of the anchor element, the element the tip describes, and returns a
    /// non-owning observer, as FlyoutControl::set_anchor() and ScrollControl::set_child() do.
    /// Replaces and destroys any previous anchor.
    template <typename T>
    T* set_anchor(std::unique_ptr<T> anchor)
    {
        static_assert(std::is_base_of_v<Element, T>, "ToolTip anchor must be an Element");
        T* observer = anchor.get();
        set_parent_of(*observer, this);
        anchor_ = std::move(anchor);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The anchor, or null before set_anchor(). Owned by this ToolTip.
    [[nodiscard]] Element* anchor() const
    {
        return anchor_.get();
    }

    /// Takes ownership of the floating hover content and returns a non-owning observer. The
    /// content is not part of the main tree. The window lays it out and draws it separately. The
    /// tooltip's background, border and padding come from the theme, so the content needs none.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "ToolTip content must be an Element");
        T* observer = content.get();
        set_parent_of(*observer, this);
        content_ = std::move(content);
        sync_popup();
        return observer;
    }
    /// The floating content, or null before set_content(). Owned by this ToolTip.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// Whether the tip is showing. It opens once the hover delay elapses or show_at() is called.
    /// It closes when the pointer leaves, on a click, on close(), or when the popup is dismissed.
    [[nodiscard]] bool is_open() const
    {
        return open_;
    }

    /// How long the pointer must stay over the anchor before the tooltip opens; 800 ms by default.
    [[nodiscard]] Duration delay() const
    {
        return Duration{delay_ms_};
    }
    /// Sets the delay. It also applies to a countdown already running, which keeps its elapsed
    /// time.
    void set_delay(Duration delay)
    {
        delay_ms_ = delay.count();
    }

    /// Which side of the dock rect the tooltip opens against; Above by default. It flips to Below
    /// near the top edge of the window.
    [[nodiscard]] PopupPlacement placement() const
    {
        return placement_;
    }
    /// Sets the side. Takes effect the next time the popup is placed.
    void set_placement(PopupPlacement placement)
    {
        placement_ = placement;
        sync_popup();
    }

    /// How far the dock rect is inflated on the left and right before the tooltip is placed
    /// against it, in DIPs; 20 by default. vertical_offset() does the same for the top and bottom.
    [[nodiscard]] float horizontal_offset() const
    {
        return horizontal_offset_;
    }
    /// Sets the horizontal inflation in DIPs. A negative value shrinks the dock rect, pulling the
    /// tip over it.
    void set_horizontal_offset(float offset)
    {
        horizontal_offset_ = offset;
        sync_popup();
    }
    /// The vertical inflation in DIPs; 20 by default. See horizontal_offset().
    [[nodiscard]] float vertical_offset() const
    {
        return vertical_offset_;
    }
    /// Sets the vertical inflation in DIPs.
    void set_vertical_offset(float offset)
    {
        vertical_offset_ = offset;
        sync_popup();
    }

    /// The placement rect, in the anchor's own coordinates. The tooltip docks to it instead of
    /// the pointer, so it opens beside the rect and doesn't cover what it describes.
    [[nodiscard]] const std::optional<Rect>& placement_rect() const
    {
        return placement_rect_;
    }
    /// Sets or clears the placement rect. nullopt docks to the pointer again.
    void set_placement_rect(std::optional<Rect> rect)
    {
        placement_rect_ = rect;
        sync_popup();
    }
    /// Focusable exactly when the anchor is; focus itself goes to the anchor.
    [[nodiscard]] bool focusable() const override;
    /// The anchor, if any. The tip's content is popup content, outside the tree.
    void for_each_child(ChildVisitor visit) const override
    {
        if (anchor_)
        {
            visit(*anchor_);
        }
    }
    /// The tip stands in for its anchor as one tab stop. Focus itself goes to the anchor.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }
    /// No automation children: the anchor is not listed under the tip.
    void automation_children(std::vector<Element*>&) const override
    {
    }

    // ---- automation ----
    /// The anchor's role, name and providers, so screen readers see the anchor. With no anchor,
    /// a Group with nothing to act on.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return anchor_ ? anchor_->automation_role() : AutomationRole::Group;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return anchor_ ? anchor_->automation_name() : std::string{};
    }

    /// Opens the tip now, docked to `pointer` (window DIPs) as a tip opened by the mouse is,
    /// whatever the pointer does over the anchor. The window shows Element::set_tool_tip() text
    /// this way. Calling it while open moves the dock point.
    void show_at(Point pointer);
    /// Closes the tip and resets the hover countdown. Nothing changes on screen when it is
    /// already closed.
    void close()
    {
        hide();
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Forwards every event to the anchor and tracks hover. A move starts the countdown, leaving
    /// cancels it or closes the tip, and a press closes the tip.
    bool pointer_override(const PointerEvent& e) override;
    /// A press the anchor took closes the tip too.
    void handled_pointer_override(const PointerEvent& e) override;
    /// Ticks the anchor, then advances the hover countdown and opens the tip once it reaches
    /// delay().
    void tick_override(Duration delta) override;


private:
    // Automation sees the anchor: its providers are this control's, and with no anchor there
    // are none.
    void automation_providers(AutomationProviders& out) override
    {
        if (anchor_)
        {
            out = detail::TreeAccess::automation_providers(*anchor_);
        }
    }
    friend class ToolTipSurface;

    void hide();
    // Opens, moves or closes the popup to match open_, the content and the dock settings.
    void                     sync_popup();
    std::unique_ptr<Element> anchor_;
    std::unique_ptr<Element> content_;
    bool                     open_              = false;
    bool                     hovered_           = false;
    double                   elapsed_ms_        = 0.0;
    double                   delay_ms_          = 800.0;
    PopupPlacement           placement_         = PopupPlacement::Above;
    float                    horizontal_offset_ = 20.0f;
    float                    vertical_offset_   = 20.0f;
    std::optional<Rect>      placement_rect_{};
    // The pointer's last position over the anchor, and where it was when the tooltip opened: the
    // dock point, fixed for as long as the tooltip stays open.
    Point                last_pointer_{};
    std::optional<Point> dock_point_{};

    // Built on the first open and kept. It reads content_ live, so a later
    // set_content() never leaves it dangling.
    std::unique_ptr<Element> surface_;

protected:
    bool key_override(const KeyEvent& e) override;
    bool text_override(const TextInputEvent& text_event) override;
    void focus_changed(bool focused) override;
    void focus_visible_changed(bool visible) override;

protected:
    /// None of its own: the anchor inside it draws its own focus ring.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }
};

/// A hover tip over an anchor. Builds a ui::ToolTipControl.
///
/// The built element wraps the anchor, which lays out and takes input as usual; the tip shows
/// after the pointer rests on it for `.delay` and hides on a press, a key or the pointer
/// leaving.
///
/// Example:
/// @code
/// window.set_content(ui::ToolTip{
///     .placement = ui::PopupPlacement::Below,
///     .anchor    = ui::Button{ .text = "Open", .on_click = [&] { open_file(); } },
///     .content   = ui::Text{ .text = "Open a document from disk" },
/// });
/// @endcode
struct ToolTip
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ToolTip>` points at.
    using control_type = ToolTipControl;

    // ---- properties ----
    /// How long the pointer must rest on the anchor before the tip opens; 800 ms by default.
    Binding<Duration> delay{Duration{800.0}};
    /// Which side of the pointer (or `placement_rect`) the tip sits on.
    Binding<PopupPlacement> placement{PopupPlacement::Above};
    /// How far the dock rect is inflated on the left and right, in DIPs.
    Binding<float> horizontal_offset{20.0f};
    /// How far the dock rect is inflated on the top and bottom, in DIPs.
    Binding<float> vertical_offset{20.0f};
    /// A rect in the anchor's coordinates to dock to instead of the pointer.
    Binding<std::optional<Rect>> placement_rect{};

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
    ControlOut<ToolTip> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The element the tip belongs to; it takes the ToolTip's place in the layout.
    Spec anchor{};
    /// What the tip shows, usually a ui::Text.
    Spec content{};

    /// Consumes the spec and returns the built ui::ToolTipControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

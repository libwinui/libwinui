// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
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

class FlyoutSurface; // defined in Flyout.cpp

/// Whether an open flyout takes keyboard focus.
enum class FlyoutShowMode
{
    Standard,  ///< takes keyboard focus while open
    Transient, ///< leaves focus where it is
};

/// Where a flyout is in its open and close cycle (FlyoutBase::phase()). During Closing, is_open()
/// is already false, but the popup keeps drawing and fades out until the close transition ends.
enum class FlyoutPhase
{
    Closed,  ///< not shown
    Opening, ///< shown, fading in
    Open,    ///< shown
    Closing, ///< committed to closing, fading out
};

/// What every flyout shares: an owned anchor element it opens from, opening and closing,
/// placement against the anchor or at a point, focus handling, and the open and close fade.
///
/// A FlyoutBase wraps its anchor. Layout, rendering, input, focus and automation pass through to
/// the anchor, and the flyout manages the popup. A Button anchor opens the flyout on click and
/// with Enter or Space by itself (ButtonControl::set_flyout()). Any other anchor opens it on a left
/// click that nothing inside the anchor handled. So a Text, an Image or a card opens it by
/// pointer, while a CheckBox or a TextBox inside still does only its own thing. Such an anchor has
/// no keyboard shortcut to open it, so call show() or toggle() from its own handler for that.
///
/// By default the popup opens below the anchor, centered and 4 DIPs away from it, and flips when
/// there is no room. show_at() puts its top-left corner at a point instead, as a context menu
/// does. FlyoutBase can't be constructed directly. Use Flyout, MenuFlyout or CommandBarFlyout.
///
/// Example:
/// @code
/// auto  flyout = std::make_unique<ui::FlyoutControl>();
/// flyout->set_anchor(std::make_unique<ui::ButtonControl>("Options")); // clicks toggle the flyout
/// flyout->set_content(std::make_unique<ui::TextControl>("Hello"));
/// flyout->set_placement(ui::PopupPlacement::Above);
/// flyout->set_placement_alignment(ui::PopupPlacementAlignment::Leading);
/// flyout->on_closed([](ui::PopupCloseReason) { std::println("flyout closed"); });
/// window.set_content(std::move(flyout));
/// @endcode
class FlyoutBase : public Element
{
public:
    /// Takes ownership of the anchor element, replacing any previous one, and returns a non-owning
    /// observer, as ScrollControl::set_child() does. A Button anchor toggles the flyout on click.
    template <typename T>
    T* set_anchor(std::unique_ptr<T> anchor)
    {
        static_assert(std::is_base_of_v<Element, T>, "Flyout anchor must be an Element");
        T* observer = anchor.get();
        set_parent_of(*observer, this);
        anchor_ = std::move(anchor);
        adopt_anchor(observer);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The anchor element (non-owning), or nullptr.
    [[nodiscard]] Element* anchor() const
    {
        return anchor_.get();
    }

    /// True while the flyout is shown or fading in. It turns false as soon as hide(), an outside
    /// click or Escape commits to closing, though the popup keeps drawing while it fades out
    /// (transition_opacity()).
    [[nodiscard]] bool is_open() const
    {
        return phase_ == FlyoutPhase::Opening || phase_ == FlyoutPhase::Open;
    }
    /// Opens the flyout against the anchor, on the side placement() names, and raises
    /// on_opened(). Does nothing while already open.
    void show();
    /// Opens the flyout with its top-left corner at `point` (window-client DIPs), as a context
    /// menu opens at the pointer.
    void show_at(Point point);
    /// Closes with PopupCloseReason::Programmatic. on_closing() can cancel it. Otherwise the close
    /// fade starts, is_open() turns false at once, and on_closed() runs once the fade ends. Does
    /// nothing while already closed.
    void hide();
    /// hide() when open, show() otherwise.
    void toggle();

    /// Which side of the anchor the content opens against; PopupPlacement::Below by default. It
    /// flips to the opposite side when there is no room.
    [[nodiscard]] PopupPlacement placement() const
    {
        return placement_;
    }
    /// Sets the side; applies the next time the popup is placed.
    void set_placement(PopupPlacement placement)
    {
        placement_ = placement;
        sync_popup();
    }

    /// The alignment across the placement axis; Center by default.
    [[nodiscard]] PopupPlacementAlignment placement_alignment() const
    {
        return alignment_;
    }
    /// Sets the alignment across the placement axis.
    void set_placement_alignment(PopupPlacementAlignment alignment)
    {
        alignment_ = alignment;
        sync_popup();
    }

    /// Whether an open flyout takes keyboard focus; Standard (it does) by default.
    [[nodiscard]] FlyoutShowMode show_mode() const
    {
        return show_mode_;
    }
    /// Sets whether the next open takes keyboard focus.
    void set_show_mode(FlyoutShowMode mode)
    {
        show_mode_ = mode;
        sync_popup();
    }

    /// Whether an outside click or Escape closes the flyout; true by default. Off, it stays open
    /// until hide() or a command in it closes it, and an outside click reaches what is below.
    [[nodiscard]] bool light_dismiss() const
    {
        return light_dismiss_;
    }
    /// Turns light dismiss on or off.
    void set_light_dismiss(bool enabled)
    {
        light_dismiss_ = enabled;
        sync_popup();
    }

    /// Raised when show() or show_at() opens the flyout.
    void on_opened(Callback<void()> handler)
    {
        on_opened_ = std::move(handler);
    }
    /// Raised with the reason before an open flyout closes; return ui::Cancel to keep it open.
    void on_closing(Callback<Decision(const PopupClosingArgs&)> handler)
    {
        on_closing_ = std::move(handler);
    }
    /// Raised with the reason once the flyout has closed and its close fade has finished (not
    /// when on_closing() canceled). The handler may destroy the flyout.
    void on_closed(Callback<void(PopupCloseReason)> handler)
    {
        on_closed_ = std::move(handler);
    }
    [[nodiscard]] bool focusable() const override;
    bool               handle_global_shortcut(const KeyEvent& e) override
    {
        return anchor_ && anchor_->handle_global_shortcut(e);
    }
    /// A right-click lands on the anchor, though the flyout holds its place in the tab order. The
    /// anchor's context request handler runs first, then the flyout's own.
    bool raise_context_requested(std::optional<Point> position) override
    {
        return (anchor_ && anchor_->raise_context_requested(position)) || Element::raise_context_requested(position);
    }
    /// Visits the anchor, if any. The popup is not a child.
    void for_each_child(ChildVisitor visit) const override
    {
        if (anchor_)
        {
            visit(*anchor_);
        }
    }
    /// The flyout stands in for its anchor as one tab stop. Focus itself goes to the anchor.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }
    /// No automation children: the anchor is not listed under the flyout.
    void automation_children(std::vector<Element*>&) const override
    {
    }

    /// A linear fade over the open and close transitions by default. A plain Flyout has no
    /// opening slide.
    [[nodiscard]] float transition_opacity() const override;

    // ---- automation ----
    /// Passes through to the anchor, like every Element override above. Screen readers see the
    /// anchor, with the open popup's content as its child.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return anchor_ ? anchor_->automation_role() : AutomationRole::Group;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return anchor_ ? anchor_->automation_name() : std::string{};
    }

protected:
    // Automation sees the anchor: its providers are this control's, and with no anchor there
    // are none.
    void automation_providers(AutomationProviders& out) override
    {
        if (anchor_)
        {
            out = detail::TreeAccess::automation_providers(*anchor_);
        }
    }
    FlyoutBase() = default;

    /// The current phase, for a derived flyout's rendering. The popup stays open through
    /// Closing, so it fades out.
    [[nodiscard]] FlyoutPhase phase() const
    {
        return phase_;
    }
    /// How far the current Opening or Closing transition is, 0 to 1 (1 once Open or Closed).
    [[nodiscard]] float transition_progress() const;
    /// The open and close transitions' lengths; 250 ms each by default. A derived flyout with its
    /// own motion sets them in its constructor.
    void set_transition_durations(Duration open, Duration close)
    {
        open_ms_  = open.count();
        close_ms_ = close.count();
    }
    /// Whether the current or last open was show_at(), placed at a point, instead of show().
    [[nodiscard]] bool positioned_at_point() const
    {
        return point_.has_value();
    }

    /// Closes for `reason`: raises on_closing(), and unless a handler cancels, starts the close
    /// fade. A derived flyout closes with PopupCloseReason::Invoked once a command in it ran.
    void close(PopupCloseReason reason);

    /// Called as show() or show_at() commits to opening, before the Opening transition starts.
    /// Reset the popup's own state here.
    virtual void opening_override()
    {
    }
    /// Called as a close commits, once no on_closing() handler canceled it.
    virtual void closing_override()
    {
    }
    /// The element the flyout floats while it shows: its surface, built on first call. Null
    /// floats nothing.
    [[nodiscard]] virtual Element* flyout_surface() = 0;
    /// The popup's element, which the flyout ticks and clears of dirty flags itself because it is
    /// not in the window's tree. flyout_surface() by default.
    [[nodiscard]] virtual Element* popup_element()
    {
        return flyout_surface();
    }
    /// The shadow the window draws behind the surface; the theme's flyout shadow by default.
    virtual void flyout_chrome(PopupChrome& chrome)
    {
        (void)chrome;
    }
    /// An outside click or Escape: closes with PopupCloseReason::LightDismiss by default.
    virtual void flyout_dismissed()
    {
        close(PopupCloseReason::LightDismiss);
    }
    /// Opens, updates or closes the popup to match the phase, the anchor and the settings. A
    /// derived flyout calls it when its surface or chrome changes.
    void sync_popup();

    // ---- Element: pure passthrough to anchor_ ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Clears the anchor, and the popup too, which is outside the tree Window clears.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Ticks the anchor, the popup while it shows, and the open/close transition.
    void tick_override(Duration delta) override;


private:
    std::unique_ptr<Element> anchor_;
    FlyoutPhase              phase_     = FlyoutPhase::Closed;
    double                   phase_ms_  = 0.0;
    double                   open_ms_   = 250.0; // Theme::anim_normal; tick_override() has no Theme
    double                   close_ms_  = 250.0;
    PopupPlacement           placement_ = PopupPlacement::Below;
    PopupPlacementAlignment  alignment_ = PopupPlacementAlignment::Center;
    FlyoutShowMode           show_mode_ = FlyoutShowMode::Standard;
    std::optional<Point>     point_{}; // show_at()'s position; nullopt when placed against the anchor
    PopupCloseReason         close_reason_   = PopupCloseReason::Programmatic; // for on_closed()
    bool                     light_dismiss_  = true;
    bool                     closing_        = false; // re-entrancy guard for close()
    bool                     anchor_pressed_ = false; // a press on the anchor nothing handled
    Callback<void()>         on_opened_;
    Callback<Decision(const PopupClosingArgs&)> on_closing_;
    Callback<void(PopupCloseReason)>            on_closed_;

    void adopt_anchor(Element* anchor); // a Button anchor opens this on click (ButtonControl::set_flyout())
    void begin_open();
    void begin_close(PopupCloseReason reason);

protected:
    /// A left click on the anchor that nothing inside it handled opens the flyout while it is
    /// closed. Presses on the open popup's content never open it again.
    bool pointer_override(const PointerEvent& e) override;
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

/// Arbitrary content floating in a popup next to an owned anchor.
///
/// The content sits on a surface with the theme's elevated menu background and a 1 DIP frame. The
/// padding is 16, 15, 16 and 17 DIPs (left, top, right, bottom). The surface is at least 96x40 and
/// at most 456x758 DIPs, so long text wraps at 456. Opening, placement and focus work as in
/// FlyoutBase. A Button anchor opens it on click, and any other anchor on a click nothing inside
/// it handled.
///
/// Example:
/// @code
/// // Declarative:
/// ui::ObservableVector<std::string> cart;
/// ui::Control<ui::Flyout>           flyout;
/// window.set_content(ui::Flyout{
///     .placement = ui::PopupPlacement::Right,
///     .control   = flyout,
///     .anchor    = ui::Button{.text = "Empty cart"},
///     .content   = ui::VerticalStack{
///         .spacing  = 12.0f,
///         .content  = {
///             ui::Text{.text = "All items will be removed. Continue?"},
///             ui::Button{.text = "Yes, empty my cart", .on_click = [&] { cart.clear(); flyout->hide(); }},
///         },
///     },
/// });
///
/// // Imperative:
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto owned = std::make_unique<ui::FlyoutControl>();
/// owned->set_anchor(std::make_unique<ui::ButtonControl>("Open"));
/// owned->set_content(std::make_unique<ui::TextControl>("Flyout content"));
/// stack->add(std::move(owned));
/// @endcode
class FlyoutControl final : public FlyoutBase
{
public:
    /// A flyout with no anchor and no content.
    FlyoutControl() = default;

    /// Takes ownership of the floating content, replacing any previous one, and returns a
    /// non-owning observer. The content shows while is_open(). It is not part of the main tree.
    /// The window lays it out and draws it separately, in its popup layer.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "Flyout content must be an Element");
        T* observer = content.get();
        set_parent_of(*observer, this);
        content_ = std::move(content);
        sync_popup();
        return observer;
    }
    /// The floating content (non-owning), or nullptr.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

private:
    // The surface wrapped around the content, built on first call. It paints the flyout's
    // background (theme.surfaces.menu) and frame behind the content.
    [[nodiscard]] Element* flyout_surface() override;

    friend class FlyoutSurface; // reads content_ live

    std::unique_ptr<Element> content_;

    /// Built on the first open and kept for this control's lifetime. It reads
    /// content_ live, so a later set_content() never leaves it dangling.
    std::unique_ptr<Element> surface_;

    [[nodiscard]] Element* popup_element() override
    {
        return content_.get();
    }
};

/// An anchor element with content that floats above the tree when opened. Builds a
/// ui::FlyoutControl. A Button anchor opens it on click, Enter or Space by itself. Any other anchor
/// opens it on a click that nothing inside it handled, by pointer only. From code, call show() on
/// the control from `.control`. It opens below the anchor, and an outside click or Escape closes
/// it.
///
/// Example:
/// @code
/// ui::ObservableVector<std::string> cart;
/// ui::Control<ui::Flyout>           confirm;
///
/// window.set_content(ui::Flyout{
///     .control = confirm,
///     .anchor  = ui::Button{ .text = "Empty cart" },
///     .content = ui::VerticalStack{
///         .spacing  = 12,
///         .content  = {
///             ui::Text{ .text = "All items will be removed. Do you want to continue?" },
///             ui::Button{ .text = "Yes, empty my cart", .on_click = [&] { cart.clear(); confirm->hide(); } },
///         },
///     },
/// });
/// @endcode
struct Flyout
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Flyout>` points at.
    using control_type = FlyoutControl;

    // ---- properties ----
    /// Whether the flyout is showing. Two-way with ui::bind(): closing it writes false, and a
    /// false that `on_closing` cancels is put back to true.
    TwoWayField<bool> is_open{false};
    /// Which side of the anchor it opens on; PopupPlacement::Below by default.
    Binding<PopupPlacement> placement{PopupPlacement::Below};
    /// How the flyout lines up with the anchor along that side.
    Binding<PopupPlacementAlignment> placement_alignment{PopupPlacementAlignment::Center};
    /// Standard takes keyboard focus while open, Transient leaves it where it is.
    Binding<FlyoutShowMode> show_mode{FlyoutShowMode::Standard};
    /// Whether an outside click or Escape closes it; true by default. Off, it stays open until
    /// `is_open` turns false, hide() runs or a command in it runs.
    Binding<bool> light_dismiss{true};

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
    /// The flyout is showing. A two-way `is_open` has been written by then.
    Action on_opened{};
    /// Gets why it is about to close; return ui::Cancel to keep it open.
    Callback<Decision(const PopupClosingArgs&)> on_closing{};
    /// Gets why it closed, once the close fade has finished. A two-way `is_open` has been written
    /// by then.
    Callback<void(PopupCloseReason)> on_closed{};
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
    ControlOut<Flyout> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The in-tree element the flyout hangs from.
    Spec anchor{};
    /// The floating content. It is not part of the main tree. The window lays it out and draws it
    /// separately, in its popup layer.
    Spec content{};

    /// Consumes the spec and returns the built ui::FlyoutControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

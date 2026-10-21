// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// How a SidePane's pane is shown.
enum class SidePaneDisplayMode
{
    Overlay,        ///< Closed: hidden. Open: the pane lies over the content.
    Inline,         ///< Closed: hidden. Open: the content shrinks to make room beside the pane.
    CompactOverlay, ///< Closed: a compact strip. Open: the whole pane lies over the content.
    CompactInline,  ///< Closed: a compact strip. Open: the whole pane sits beside the content.
};

/// Which side a SidePane's pane is on.
enum class SidePanePlacement
{
    Left,  ///< The pane on the left, the content to its right.
    Right, ///< The pane on the right.
};

/// Whether a light-dismissable surface dims what is behind it.
enum class LightDismissOverlayMode
{
    Auto, ///< Behaves as Off.
    On,   ///< Dim what is behind the open surface.
    Off,  ///< Never dim.
};

/// A content area with a pane beside it that opens, closes and collapses to a compact strip.
///
/// The view owns both the pane and the content. In the overlay modes an open pane closes on a
/// click on the content or on Escape. That raises on_pane_closing(), which may cancel, and then
/// closes the pane. Opening an overlay pane moves keyboard focus into it and keeps Tab there. The
/// pane slides open and shut while the window animates, and changes at once otherwise. The
/// declarative form is ui::SidePane.
///
/// Example:
/// @code
/// ui::Property<bool> open{false};
///
/// window.set_content(ui::VerticalStack{ .content = {
///     ui::Button{ .text = "Toggle pane", .on_click = [&] { open.toggle(); } },
///     ui::SidePane{
///         .is_pane_open   = open,
///         .display_mode   = ui::SidePaneDisplayMode::CompactOverlay,
///         .height         = 300,
///         .on_pane_closed = [&] { open = false; }, // a light dismiss closes it without the binding
///         .pane           = ui::Text{ .text = "PANE CONTENT", .margin = ui::Edges{.left = 60, .top = 12} },
///         .content        = ui::Text{ .text = "Main content", .margin = ui::Thickness{12} },
///     },
/// }});
///
/// // Imperatively:
/// auto split = std::make_unique<ui::SidePaneControl>();
/// split->set_display_mode(ui::SidePaneDisplayMode::Inline);
/// split->set_pane(std::make_unique<ui::TextControl>("Pane"));
/// split->set_content(std::make_unique<ui::TextControl>("Content"));
/// split->set_pane_open(true);
/// @endcode
class SidePaneControl final : public Element
{
public:
    /// An Overlay view with the pane closed on the left, 320 DIPs open, 48 compact.
    SidePaneControl() = default;

    /// Takes ownership of the pane's element and returns a non-owning observer. Null clears it.
    /// Lays the view out again.
    template <typename T>
    T* set_pane(std::unique_ptr<T> pane)
    {
        static_assert(std::is_base_of_v<Element, T>, "SidePane pane must be an Element");
        T* observer = pane.get();
        if (observer)
        {
            set_parent_of(*observer, this);
        }
        pane_ = std::move(pane);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The pane's element (owned by the view), or null.
    [[nodiscard]] Element* pane() const
    {
        return pane_.get();
    }

    /// Takes ownership of the main area's element and returns a non-owning observer. Null clears
    /// it. Lays the view out again.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "SidePane content must be an Element");
        T* observer = content.get();
        if (observer)
        {
            set_parent_of(*observer, this);
        }
        content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The content's element (owned by the view), or null.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// Whether the pane is open. Opening raises on_pane_opening, then on_pane_opened. Closing raises
    /// on_pane_closing, which cannot cancel a close made this way, then on_pane_closed.
    [[nodiscard]] bool is_pane_open() const
    {
        return pane_open_;
    }
    /// Opens or closes the pane and starts the slide. Does nothing when the pane is already there.
    void set_pane_open(bool open);

    /// How the pane shows and whether it pushes the content; Overlay by default.
    [[nodiscard]] SidePaneDisplayMode display_mode() const
    {
        return display_mode_;
    }
    /// Changes the display mode and lays the view out again.
    void set_display_mode(SidePaneDisplayMode mode);

    /// Which side the pane is on; Left by default.
    [[nodiscard]] SidePanePlacement pane_placement() const
    {
        return pane_placement_;
    }
    /// Moves the pane to the given side and lays the view out again.
    void set_pane_placement(SidePanePlacement placement)
    {
        pane_placement_ = placement;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The open pane's width in DIPs, 320 by default; nullopt is Auto, the pane's own width.
    [[nodiscard]] std::optional<float> open_pane_length() const
    {
        return open_pane_length_;
    }
    /// Changes the open width, in DIPs, and lays the view out again.
    void set_open_pane_length(OptionalFloat length)
    {
        open_pane_length_ = length;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The compact strip's width in DIPs, 48 by default.
    [[nodiscard]] float compact_pane_length() const
    {
        return compact_pane_length_;
    }
    /// Changes the compact strip's width, in DIPs, and lays the view out again.
    void set_compact_pane_length(float length)
    {
        compact_pane_length_ = length;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The pane's fixed fill; nullopt uses the theme's pane color.
    [[nodiscard]] std::optional<Color> pane_background() const
    {
        return pane_background_.color();
    }
    /// The theme color the pane is filled from, or null.
    [[nodiscard]] ThemeColor pane_background_resource() const
    {
        return pane_background_.resource();
    }
    /// Sets or clears the pane's fixed fill, replacing any theme color. Redraws.
    void set_pane_background(std::optional<Color> color)
    {
        pane_background_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Fills the pane with a theme color read each time it is drawn, replacing any fixed color.
    void set_pane_background(ThemeColor resource)
    {
        pane_background_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether an open overlay pane dims the content behind it. Only On dims. Auto by default.
    [[nodiscard]] LightDismissOverlayMode light_dismiss_overlay_mode() const
    {
        return light_dismiss_overlay_mode_;
    }
    /// Changes the overlay mode and redraws.
    void set_light_dismiss_overlay_mode(LightDismissOverlayMode mode)
    {
        light_dismiss_overlay_mode_ = mode;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The pane is about to open.
    void on_pane_opening(Callback<void()> handler)
    {
        on_pane_opening_ = std::move(handler);
    }
    /// Raised right after on_pane_opening, as the slide starts.
    void on_pane_opened(Callback<void()> handler)
    {
        on_pane_opened_ = std::move(handler);
    }
    /// The pane is about to close. Return ui::Cancel to keep it open. Only a light dismiss can be
    /// canceled. Closing through set_pane_open(false) raises it but closes regardless.
    void on_pane_closing(Callback<Decision()> handler)
    {
        on_pane_closing_ = std::move(handler);
    }
    /// The pane has closed, however that happened. Use it to learn about a light dismiss when
    /// `is_pane_open` is not bound two-way.
    void on_pane_closed(Callback<void()> handler)
    {
        on_pane_closed_ = std::move(handler);
    }

    /// Whether a click outside the pane or Escape would close it right now. True in an overlay
    /// mode with the pane open.
    [[nodiscard]] bool can_light_dismiss() const;

    /// The pane's column at its full open length, from the last arrange.
    [[nodiscard]] const Rect& pane_bounds() const
    {
        return pane_rect_;
    }
    /// The content's column, from the last arrange.
    [[nodiscard]] const Rect& content_bounds() const
    {
        return content_rect_;
    }
    /// The part of the pane that is showing right now, animation included; empty while hidden.
    [[nodiscard]] Rect visible_pane_bounds() const;
    /// How far open the pane is, from 0 (closed) to 1 (open), eased.
    [[nodiscard]] float pane_progress() const;
    /// The pane's tab stops, then the content's. Only the pane's while it holds focus.
    void collect_focusables(std::vector<Element*>& out) override;
    /// Lists the content, then the pane, whether or not any of the pane shows.
    void for_each_child(ChildVisitor visit) const override;
    /// The content, then the pane while any of it shows.
    void sync_native(NativeHost& host) override;
    /// The content, then the pane while any of it shows.
    void automation_children(std::vector<Element*>& out) const override;

protected:
    // ---- Element ----
    /// Measures the pane, in the whole room for an Auto open length, and the content in what the
    /// display mode leaves it. Fills the available size.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the pane column at the open length and the content beside or under it.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the content, the dimming overlay when it is on, then the visible part of the pane.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the slide and ticks the content, then the pane while any of it shows.
    void tick_override(Duration delta) override;
    /// A release on the light-dismiss layer closes the pane.
    bool pointer_override(const PointerEvent& e) override;
    /// The pane over the content.
    void hit_test_children(Point p, std::vector<Element*>& out) const override;
    /// The light-dismiss layer, and the part of a sliding pane that is not shown yet.
    [[nodiscard]] bool covers_children_at(Point p) const override;
    /// Enter as a light-dismissable pane opens, Restore as it closes. The Window moves focus.
    [[nodiscard]] FocusRequest take_focus_request() override;
    /// The pane: where an Enter request puts focus.
    [[nodiscard]] Element* focus_request_root() override
    {
        return pane_.get();
    }


private:
    // Itself, then the content's live elements and, while any of the pane shows, the pane's.
    void                collect_live(std::vector<Element*>& out) override;
    [[nodiscard]] bool  light_dismissible() const;
    [[nodiscard]] bool  compact() const;
    [[nodiscard]] bool  pane_live() const;
    [[nodiscard]] float resolved_open_length() const;
    /// Light-dismiss close: on_pane_closing may cancel it.
    void try_light_dismiss();
    void place_children();
    void update_focus_trap();

    std::unique_ptr<Element> pane_;
    std::unique_ptr<Element> content_;
    bool                     pane_open_      = false;
    SidePaneDisplayMode      display_mode_   = SidePaneDisplayMode::Overlay;
    SidePanePlacement        pane_placement_ = SidePanePlacement::Left;
    std::optional<float>     open_pane_length_{320.0f};
    float                    compact_pane_length_ = 48.0f;
    Paint                    pane_background_;
    LightDismissOverlayMode  light_dismiss_overlay_mode_ = LightDismissOverlayMode::Auto;

    Callback<void()>     on_pane_opening_;
    Callback<void()>     on_pane_opened_;
    Callback<Decision()> on_pane_closing_;
    Callback<void()>     on_pane_closed_;

    // Linear 0..1 openness clock; pane_progress() eases it. Like Expander's slide, it only runs in a
    // host that drives tick_override(); until then every change settles at once.
    double progress_ = 0.0;
    bool   ticking_  = false;
    // Which curve the running transition uses: the overlay one or the inline one.
    bool overlay_transition_ = true;

    // Focus: whether the pane holds the ring, and the request not yet handed to Window.
    bool         trapping_        = false;
    FocusRequest pending_request_ = FocusRequest::None;

    mutable float pane_measured_width_ = 0.0f;
    Rect          pane_rect_{};
    Rect          content_rect_{};

protected:
    /// Escape light-dismisses an open overlay pane.
    bool key_override(const KeyEvent& e) override;
};

/// A content area and a pane that opens beside or over it. Builds a ui::SidePaneControl.
/// `.open_pane_length` unset is Auto, the pane's own width. `.is_pane_open = prop` is two-way. A
/// light dismiss closes the pane and writes false into the property before `.on_pane_closed` runs.
///
/// Example:
/// @code
/// ui::Property<bool> open{false};
///
/// window.set_content(ui::SidePane{
///     .is_pane_open   = open,
///     .display_mode   = ui::SidePaneDisplayMode::CompactOverlay,
///     .height         = 400.0f,
///     .pane           = ui::VerticalStack{.content = {ui::Text{.text = "Navigation"}}},
///     .content        = ui::VerticalStack{.content = {
///         ui::Button{.text = "Toggle pane", .on_click = [&] { open.toggle(); }},
///     }},
/// });
/// @endcode
struct SidePane
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<SidePane>` points at.
    using control_type = SidePaneControl;

    // ---- properties ----
    /// Whether the pane is open.
    TwoWayField<bool> is_pane_open{false};
    /// How the pane shows: Overlay (the default), Inline, CompactOverlay or CompactInline.
    Binding<SidePaneDisplayMode> display_mode{SidePaneDisplayMode::Overlay};
    /// The side the pane is on.
    Binding<SidePanePlacement> pane_placement{SidePanePlacement::Left};
    /// The open pane's width, DIPs; unset is Auto.
    Binding<std::optional<float>> open_pane_length{320.0f};
    /// The strip a Compact* mode shows while closed, DIPs.
    Binding<float> compact_pane_length{48.0f};
    /// Whether an open overlay pane dims the content. Auto does not dim.
    Binding<LightDismissOverlayMode> light_dismiss_overlay_mode{LightDismissOverlayMode::Auto};
    /// The pane's fill, a Color or a ThemeColor. Empty (the default) uses the theme's pane color.
    Binding<Paint> pane_background{};

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
    /// The pane is about to open.
    Callback<void()> on_pane_opening{};
    /// Raised right after `.on_pane_opening`, as the slide starts.
    Callback<void()> on_pane_opened{};
    /// The pane is about to close. Return ui::Cancel to keep it open. Only a light dismiss can be
    /// canceled.
    Callback<Decision()> on_pane_closing{};
    /// The pane has closed, however that happened.
    Callback<void()> on_pane_closed{};
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
    ControlOut<SidePane> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The side region that opens and closes.
    Spec pane{};
    /// The main area.
    Spec content{};

    /// Consumes the spec and returns the built ui::SidePaneControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

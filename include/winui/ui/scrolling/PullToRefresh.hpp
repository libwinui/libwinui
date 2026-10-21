// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// Which way the content is pulled.
enum class RefreshPullDirection
{
    LeftToRight, ///< Pulled rightwards from the left edge, while scrolled to the start.
    TopToBottom, ///< Pulled down from the top, while scrolled to the start (the default).
    RightToLeft, ///< Pulled leftwards from the right edge, while scrolled to the end.
    BottomToTop, ///< Pulled up from the bottom, while scrolled to the end.
};

/// How the indicator is turned.
enum class RefreshVisualizerOrientation
{
    Auto,                            ///< Turned along the pull.
    Normal,                          ///< Upright.
    Rotate90DegreesCounterclockwise, ///< Turned by +90 degrees.
    Rotate270DegreesCounterclockwise ///< Turned by -90 degrees.
};

/// Where a pull to refresh is: idle, being pulled, armed or refreshing.
enum class PullToRefreshState
{
    Idle,        ///< Not pulled and not refreshing.
    Interacting, ///< Pulled, but not far enough to refresh.
    Pending,     ///< Pulled far enough, so a release now refreshes.
    Refreshing,  ///< Refreshing until complete_refresh().
};

/// What `on_refresh_state_changed` gets: the state before and after the change.
struct PullToRefreshStateChangedArgs
{
    PullToRefreshState old_state = PullToRefreshState::Idle; ///< The state it left.
    PullToRefreshState new_state = PullToRefreshState::Idle; ///< The state it is in now.
};

/// A container that refreshes its content when it is pulled past its start, with a progress
/// indicator. The pull is a mouse drag.
///
/// A pull starts once a drag mostly along `pull_direction()` passes 8 DIPs, and only while the
/// content is scrolled to the edge the pull comes from. The content is anything with
/// `Element::scroll_info()`, such as a `Scroll` or a `ListView`. It follows the pointer, and a
/// 100 DIP visualizer slides in behind it, its indicator turning with the pull. The indicator is
/// a Refresh symbol, or what `set_visualizer_content()` installs. Releasing past 80 % of the
/// visualizer (Pending) refreshes: the state goes to Refreshing, `on_refresh_requested()` fires
/// once, and the refresh lasts until the app calls `complete_refresh()`. `ui::PullToRefresh` is
/// the declarative form.
///
/// Example:
/// @code
/// ui::Control<ui::PullToRefresh> refresh;
/// window.set_content(ui::PullToRefresh{
///     .height               = 400.0f,
///     .on_refresh_requested = [&refresh] {
///         // Start reloading; when the new data is in (now or later, e.g. from a timer):
///         refresh->complete_refresh(); // back to Idle
///     },
///     .control              = refresh,
///     .content              = ui::Scroll{.content = ui::VerticalStack{.content = {
///         ui::Text{.text = "Item 1"},
///         ui::Text{.text = "Item 2"},
///     }}},
/// });
///
/// // Imperatively, pulling up from the bottom:
/// auto ptr = std::make_unique<ui::PullToRefreshControl>();
/// ui::ScrollControl* scroll = ptr->set_content(std::make_unique<ui::ScrollControl>());
/// ptr->set_pull_direction(ui::RefreshPullDirection::BottomToTop);
/// ptr->on_refresh_state_changed([](const ui::PullToRefreshStateChangedArgs& args) { /* e.g. show a status text */ });
/// ptr->request_refresh(); // refresh programmatically, without a pull
/// @endcode
class PullToRefreshControl final : public Element
{
public:
    /// Pulls TopToBottom, with the default Refresh-symbol indicator and no content.
    PullToRefreshControl();
    ~PullToRefreshControl() override;

    // ---- content ----
    /// Sets the content that is pulled and takes ownership of it. Returns a non-owning pointer
    /// to it.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "PullToRefresh content must be an Element");
        T* observer = content.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The pulled content (non-owning), or nullptr.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// What spins as the list is pulled, instead of the Refresh symbol.
    template <typename T>
    T* set_visualizer_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "the visualizer's content must be an Element");
        T* observer = content.get();
        replace_indicator(std::move(content));
        return observer;
    }
    /// The indicator element (non-owning): the default Refresh symbol, or what
    /// `set_visualizer_content()` installed.
    [[nodiscard]] Element* visualizer_content() const
    {
        return indicator_.get();
    }

    // ---- direction ----
    /// Which edge the content is pulled from; TopToBottom by default. Changing it mid-pull ends the pull.
    [[nodiscard]] RefreshPullDirection pull_direction() const
    {
        return direction_;
    }
    /// Lays the control out again and redraws.
    void set_pull_direction(RefreshPullDirection direction);

    /// How the indicator is turned; Auto by default.
    [[nodiscard]] RefreshVisualizerOrientation visualizer_orientation() const
    {
        return orientation_;
    }
    /// Marks the control for redraw.
    void set_visualizer_orientation(RefreshVisualizerOrientation orientation)
    {
        orientation_ = orientation;
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- state ----
    /// The visualizer's current state; Idle by default.
    [[nodiscard]] PullToRefreshState state() const
    {
        return state_;
    }
    /// How far the content is pushed along the pull, in [0, 100].
    [[nodiscard]] float pull_distance() const
    {
        return pull_distance_;
    }
    /// How far the pull has come, as pull_distance() over the visualizer's 100 DIPs, in [0, 1].
    [[nodiscard]] float interaction_ratio() const;

    /// Starts a refresh at once and raises on_refresh_requested. Does nothing while refreshing.
    void request_refresh();
    /// Ends the refresh and goes back to Idle.
    void complete_refresh();

    /// Fires with the old and the new state.
    void on_refresh_state_changed(Callback<void(const PullToRefreshStateChangedArgs&)> handler)
    {
        on_refresh_state_changed_ = std::move(handler);
    }
    /// Fires when a refresh starts; the refresh lasts until complete_refresh().
    void on_refresh_requested(Callback<void()> handler)
    {
        on_refresh_requested_ = std::move(handler);
    }
    /// Visits the content only. The indicator takes no focus, so it is added separately to the
    /// visual tree and the accessibility tree while it shows (below).
    void for_each_child(ChildVisitor visit) const override
    {
        if (content_)
        {
            visit(*content_);
        }
    }

protected:
    /// Adds the indicator while it is pulled into view.
    void build_visual_tree_override(VisualTree& tree) const override;

public:
    /// The content, then the indicator while it is pulled into view.
    void automation_children(std::vector<Element*>& out) const override
    {
        Element::automation_children(out);
        if (indicator_visible())
        {
            out.push_back(indicator_.get());
        }
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Drives the pull from a mouse drag; other pointer input goes to the content.
    bool pointer_override(const PointerEvent& e) override;
    /// A press and release the content took still start and end a pull.
    void handled_pointer_override(const PointerEvent& e) override;
    /// Clears the dirty flags of the content and of the indicator, whether it shows or not.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Advances the settle, Pending pulse and Refreshing spin animations, and ticks the content
    /// and the indicator.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] bool indicator_visible() const;
    [[nodiscard]] bool can_start_pull() const;
    [[nodiscard]] bool vertical() const
    {
        return direction_ == RefreshPullDirection::TopToBottom || direction_ == RefreshPullDirection::BottomToTop;
    }
    /// +1 when the pull comes from the start of its axis (TopToBottom, LeftToRight), -1 from its end.
    [[nodiscard]] float pull_sign() const
    {
        return direction_ == RefreshPullDirection::TopToBottom || direction_ == RefreshPullDirection::LeftToRight
                   ? 1.0f
                   : -1.0f;
    }
    /// The content's rect moved `offset` along the pull.
    [[nodiscard]] Rect  pulled(const Rect& base, float offset) const;
    [[nodiscard]] float start_angle() const;
    void                end_pull();
    void                set_pull_distance(float distance);
    void                set_state(PullToRefreshState state);
    void                begin_refresh();
    void                replace_indicator(std::unique_ptr<Element> content);
    void                start_indicator_animations();
    [[nodiscard]] float indicator_angle() const;
    [[nodiscard]] float indicator_scale() const;
    [[nodiscard]] float indicator_opacity() const;
    [[nodiscard]] float content_offset() const;

    std::unique_ptr<Element> content_;
    std::unique_ptr<Element> indicator_;

    RefreshPullDirection         direction_     = RefreshPullDirection::TopToBottom;
    RefreshVisualizerOrientation orientation_   = RefreshVisualizerOrientation::Auto;
    PullToRefreshState           state_         = PullToRefreshState::Idle;
    float                        pull_distance_ = 0.0f;
    Rect                         content_base_rect_{};

    float  settle_from_   = 0.0f;
    float  settle_to_     = 0.0f;
    double settle_ms_     = -1.0; // < 0: not settling
    double pending_ms_    = 0.0;
    double refreshing_ms_ = 0.0;

    bool  pointer_down_      = false;
    bool  drag_owns_gesture_ = false;
    Point drag_start_pos_{};

    mutable const TextMeasurer* measurer_ = nullptr;
    mutable const Theme*        theme_    = nullptr;

    Callback<void(const PullToRefreshStateChangedArgs&)> on_refresh_state_changed_;
    Callback<void()>                                     on_refresh_requested_;
};

/// Pull one child with the mouse to refresh it. Builds a ui::PullToRefreshControl.
/// on_refresh_requested fires once, and the app calls `.control`'s complete_refresh() when the
/// work it started, typically an async data fetch, has finished.
///
/// Example:
/// @code
/// void start_fetch(ui::Action done); // the app's own async work
/// ui::Control<ui::PullToRefresh> pull;
///
/// window.set_content(ui::PullToRefresh{
///     .height               = 400.0f,
///     .on_refresh_requested = [&]
///     {
///         start_fetch([&] { pull->complete_refresh(); });
///     },
///     .control = pull,
///     .content = ui::Scroll{.content = ui::VerticalStack{.content = {
///         ui::Text{.text = "Item 1"},
///         ui::Text{.text = "Item 2"},
///     }}},
/// });
/// @endcode
struct PullToRefresh
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<PullToRefresh>` points at.
    using control_type = PullToRefreshControl;

    // ---- properties ----
    /// Which edge the content is pulled from.
    Binding<RefreshPullDirection> pull_direction{RefreshPullDirection::TopToBottom};
    /// How the indicator is turned. Auto turns it along the pull.
    Binding<RefreshVisualizerOrientation> visualizer_orientation{RefreshVisualizerOrientation::Auto};

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
    /// Fires once per refresh; call complete_refresh() on `.control` when done.
    Callback<void()> on_refresh_requested{};
    /// Fires with the old and the new state (Idle, Interacting, Pending, Refreshing).
    Callback<void(const PullToRefreshStateChangedArgs&)> on_refresh_state_changed{};
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
    ControlOut<PullToRefresh> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The pulled content, typically a ui::Scroll or a list.
    Spec content{};
    /// What turns as the list is pulled, in place of the Refresh symbol.
    Spec visualizer_content{};

    /// Consumes the spec and returns the built ui::PullToRefreshControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

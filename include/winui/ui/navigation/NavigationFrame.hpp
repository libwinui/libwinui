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
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// How the page on screen got there.
enum class NavigationMode
{
    /// navigate(): a new page, the old one pushed onto the back stack.
    New,
    /// go_back(): the top of the back stack came back.
    Back,
    /// go_forward(): the top of the forward stack came back.
    Forward,
};

/// Whether a page on the back stack is kept alive (Enabled) or built again
/// by its factory when it is gone back to (Disabled, the default).
enum class NavigationCacheMode
{
    /// The page is destroyed when left and rebuilt by its factory on return.
    Disabled,
    /// The page instance is kept on the stack and shown again as it was left.
    Enabled,
};

/// What the frame's on_navigated() handler gets: the page now on screen, not laid out yet, and
/// how it got there.
struct NavigationEventArgs
{
    /// The new page; owned by the frame, valid until it navigates away from it.
    Element* content = nullptr;
    /// Whether it arrived by navigate(), go_back() or go_forward().
    NavigationMode mode = NavigationMode::New;
};

/// What the frame's on_navigating_from() handler gets: the page about to leave, still on screen
/// and laid out, and how the frame is leaving it.
struct NavigatingFromEventArgs
{
    Element&       page;                       ///< The page on screen now.
    NavigationMode mode = NavigationMode::New; ///< Leaving by navigate(), go_back() or go_forward().
};

/// A page host with a back and forward stack: shows one page at a time, built by
/// a PageFactory on each navigation, and animates between pages with a NavigationTransition.
///
/// The frame owns the page on screen, and with NavigationCacheMode::Enabled also the cached pages
/// on its stacks. Layout, input, focus and rendering pass straight through to the page on screen.
/// While a transition plays, the leaving page is drawn under the arriving one and neither takes
/// pointer input. The declarative form is ui::NavigationFrame, navigated through its `.control`.
///
/// Example:
/// @code
/// auto frame_owned = std::make_unique<ui::NavigationFrameControl>();
/// frame_owned->set_content_transitions({ui::NavigationTransition{}});
/// frame_owned->on_navigated([](const ui::NavigationEventArgs& e) {
///     if (e.mode == ui::NavigationMode::Back)
///         std::println("came back");
/// });
/// ui::NavigationFrameControl* frame = window.set_content(std::move(frame_owned));
///
/// frame->navigate([] { return std::make_unique<ui::TextControl>("Home"); });
/// frame->navigate([] { return std::make_unique<ui::TextControl>("Settings"); },
///                 ui::ZoomPageTransition{});
/// if (frame->can_go_back())
///     frame->go_back(); // "Home" again, rebuilt by its factory
///
/// // Or declaratively, taking the frame through .control:
/// ui::Control<ui::NavigationFrame> page_host;
/// window.set_content(ui::NavigationFrame{
///     .content_transitions = {ui::NavigationTransition{}},
///     .control             = page_host,
/// });
/// page_host->navigate([] { return ui::Text{.text = "Home"}; });
/// @endcode
class NavigationFrameControl final : public Element
{
public:
    /// Builds a page. Called on every navigation to it, and again on going back to it unless it
    /// was cached. A callable returning a `std::unique_ptr` to an Element works, and so does one
    /// returning a spec (`[] { return ui::Text{.text = "Home"}; }`), which is built each time.
    class PageFactory : public Callback<std::unique_ptr<Element>()>
    {
        using Base = Callback<std::unique_ptr<Element>()>;

    public:
        using Base::Base;
        /// No factory; navigate() refuses it.
        PageFactory() = default;
        /// A callable that returns a spec, built into the page on each call.
        template <typename F>
            requires std::invocable<F&> && (!std::convertible_to<std::invoke_result_t<F&>, std::unique_ptr<Element>>) &&
                     std::convertible_to<std::invoke_result_t<F&>, Spec>
        PageFactory(F page) // NOLINT(google-explicit-constructor): a lambda is a factory
            :
            Base([page = std::move(page)]() mutable -> std::unique_ptr<Element> { return Spec{page()}.take(); })
        {
        }
    };

    /// An empty frame: no page, empty stacks, and no transitions (navigation is immediate).
    NavigationFrameControl() = default;

    /// Shows a new page, pushing the one on screen onto the back stack and clearing the forward
    /// stack. `transition` overrides the NavigationTransition's default. Returns false and does
    /// nothing for an empty factory or one that builds nothing.
    bool navigate(PageFactory                   page,
                  std::optional<PageTransition> transition = std::nullopt,
                  NavigationCacheMode           cache      = NavigationCacheMode::Disabled);
    /// Moves the page on screen onto the forward stack, shows the top of the back stack and plays
    /// the transition backwards. The transition is the leaving entry's own unless `transition` is given.
    /// Returns false when the back stack is empty.
    bool go_back(std::optional<PageTransition> transition = std::nullopt);
    /// Moves the page on screen back onto the back stack and shows the top of the forward stack,
    /// with the transition it was first navigated with. Returns false when the forward stack is
    /// empty.
    bool go_forward();
    /// Whether the back stack is not empty.
    [[nodiscard]] bool can_go_back() const
    {
        return !back_stack_.empty();
    }
    /// Whether the forward stack is not empty: a go_back() has not been followed by a navigate()
    /// since.
    [[nodiscard]] bool can_go_forward() const
    {
        return !forward_stack_.empty();
    }
    /// How many pages go_back() can return to.
    [[nodiscard]] std::size_t back_stack_depth() const
    {
        return back_stack_.size();
    }

    /// The page on screen, or nullptr before the first navigation.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }
    /// The page going away while its navigation transition plays, or nullptr.
    [[nodiscard]] Element* leaving() const
    {
        return leaving_;
    }

    /// The transitions; a NavigationTransition here animates navigation.
    [[nodiscard]] const TransitionList& content_transitions() const
    {
        return content_transitions_;
    }
    /// Replaces the transitions; takes effect from the next navigation.
    void set_content_transitions(TransitionList transitions)
    {
        content_transitions_ = std::move(transitions);
    }

    /// Fired after the new page is in place, before it is laid out. A page's arrival work, like
    /// starting a connected animation, goes here.
    void on_navigated(Callback<void(const NavigationEventArgs&)> handler)
    {
        on_navigated_ = std::move(handler);
    }
    /// Fired before a navigation, with the page about to leave. The page is still on screen and
    /// laid out, so this is where to prepare a connected animation from it.
    void on_navigating_from(Callback<void(const NavigatingFromEventArgs&)> handler)
    {
        on_navigating_from_ = std::move(handler);
    }
    /// Visits the page on screen only. A leaving page is never focusable, owns no popups and is
    /// not an automation child, and a NativeControl on a page left behind is hidden.
    /// clear_dirty() and tick_override() reach the leaving page too.
    void for_each_child(ChildVisitor visit) const override
    {
        if (content_)
        {
            visit(*content_);
        }
    }

protected:
    // ---- Element ----
    /// The desired size of the page on screen, or zero with no page. The leaving page is measured
    /// too.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Both the page on screen and the leaving page fill the frame's whole rect.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// The leaving page, then the page on screen over it.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Clears the frame's flags and those of both pages, the leaving one included.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Plays the leaving page's exit. Once it has finished, the page is destroyed, or kept on its
    /// stack entry if that entry caches it.
    void tick_override(Duration delta) override;


private:
    struct Entry
    {
        PageFactory                   factory;
        std::optional<PageTransition> transition;
        NavigationCacheMode           cache = NavigationCacheMode::Disabled;
        std::unique_ptr<Element>      cached; // the page itself, while cached on a stack
    };

    // The page on screen moves to `outgoing_home` (a back or forward stack slot) and away with the
    // transition; `back` picks the Back triggers.
    void show(std::unique_ptr<Element>             incoming,
              Entry*                               outgoing_home,
              const std::optional<PageTransition>& transition,
              bool                                 back,
              NavigationMode                       mode);
    [[nodiscard]] std::optional<PageTransition> effective_transition(const std::optional<PageTransition>& transition) const;
    void settle_leaving();

    std::unique_ptr<Element> content_;
    // Owned here, or by its stack entry when its navigation cached it. Entries are heap objects, so
    // the pointer survives the stacks reallocating.
    std::unique_ptr<Element> leaving_owned_;
    Element*                 leaving_ = nullptr;
    Entry                    current_{};
    std::vector<Entry>       back_stack_;
    std::vector<Entry>       forward_stack_;
    TransitionList           content_transitions_;
    Rect                     arranged_{};

    Callback<void(const NavigationEventArgs&)>     on_navigated_;
    Callback<void(const NavigatingFromEventArgs&)> on_navigating_from_;
};

/// A page host with a back stack. Builds a ui::NavigationFrameControl. The name ui::Frame belongs
/// to the window's frame style. Navigate it through `.control`, since a page is a factory and
/// navigation is an action on the frame.
///
/// Example:
/// @code
/// ui::Control<ui::NavigationFrame> frame;
/// ui::Property<bool>                back_enabled{false};
///
/// window.set_content(ui::NavigationFrame{
///     .content_transitions = { ui::NavigationTransition{} },
///     .on_navigated        = [&](const ui::NavigationEventArgs&) { back_enabled = frame->can_go_back(); },
///     .control             = frame,
/// });
/// frame->navigate([] { return ui::Text{.text = "Home"}; });
/// frame->navigate([] { return ui::Text{.text = "Settings"}; }, ui::ZoomPageTransition{});
/// frame->go_back(); // "Home" again, rebuilt by its factory
/// @endcode
struct NavigationFrame
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<NavigationFrame>` points at.
    using control_type = NavigationFrameControl;

    // ---- properties ----
    /// The transitions; a NavigationTransition animates navigation.
    Binding<TransitionList> content_transitions{};

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
    /// Called after the new page is in place.
    Callback<void(const NavigationEventArgs&)> on_navigated{};
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
    ControlOut<NavigationFrame> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::NavigationFrameControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

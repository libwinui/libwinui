// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/ThreadCheck.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/LayoutContext.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui
{

namespace testing
{
struct VisualStates;
struct ElementAccess;
} // namespace testing
class Clipboard;
class Window;
class ContextMenuHost;
class NativeHost;
class SpellChecker;
struct DragEventArgs;
struct DragResponse;
enum class DataPackageOperation : unsigned;

/// Whether an element is shown. A Collapsed element takes no room in layout, draws nothing, takes no
/// input and leaves the tab ring, along with everything under it.
enum class Visibility
{
    Visible,   ///< laid out, drawn and hit-tested (the default)
    Collapsed, ///< zero size, not drawn, no input, out of the tab ring
};

/// The theme an element (and everything under it) uses: light or dark whatever the window's is, or
/// following it (Default).
enum class ElementTheme
{
    Default, ///< whatever theme the parent hands down (the default)
    Light,   ///< the light version of that theme
    Dark,    ///< the dark version of that theme
};

// The running state of an element's visual layer (animations, transitions), private to
// src/ui/Element.cpp and only allocated once something on the element moves.
struct ElementMotion;
namespace detail
{
struct PointerRoute;
struct PointerDispatch;
// Out of line, so Element's extras can hold a PointerRoute without its definition.
struct PointerRouteDeleter
{
    void operator()(PointerRoute* route) const noexcept;
};
} // namespace detail

struct ElementMotionDeleter
{
    void operator()(ElementMotion* motion) const;
};

class Element;

/// What Element::for_each_child() calls once per child: a non-owning reference to any callable
/// taking an `Element&`, two pointers wide, so walking the tree allocates nothing. It refers to
/// the callable it was made from and must not outlive it, which a lambda passed straight to
/// for_each_child() never does.
class ChildVisitor
{
public:
    template <typename F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, ChildVisitor> && std::is_invocable_v<F&, Element&>)
    ChildVisitor(F&& visit) noexcept // NOLINT(google-explicit-constructor): a lambda is a visitor
        :
        object_(const_cast<void*>(static_cast<const void*>(std::addressof(visit)))),
        call_([](void* object, Element& child) { (*static_cast<std::remove_reference_t<F>*>(object))(child); })
    {
    }

    void operator()(Element& child) const
    {
        call_(object_, child);
    }

private:
    void* object_;
    void (*call_)(void*, Element&);
};

namespace detail
{
class PaintTracker;
// The backend's compositor seam, private to libwinui (src/render/Compositor.hpp).
class Compositor;
struct LayerPlacement;
// Element::request_tick()'s note, and Window::tick()'s read of it (which clears it), per thread.
void               note_tick_request() noexcept;
[[nodiscard]] bool take_tick_request() noexcept;
// Element::request_tick_in()'s note, the earliest wait asked for, and its read (which clears it).
void                                note_wake_request(double ms) noexcept;
[[nodiscard]] std::optional<double> take_wake_request() noexcept;
// mark_dirty()'s note, per thread, so Window::tick() can tell a change that only an element's own
// timed step made (Element::tick()) from one anything else made.
void note_dirty_mark() noexcept;
// How many mark_dirty() calls this thread has made so far.
[[nodiscard]] std::uint64_t dirty_marks() noexcept;
// Whether an Element::tick() since the last call dirtied something other than a timed step: an
// animation of its visual layer, or a tick_override() that did not ask for request_tick_in(). Clears it.
[[nodiscard]] bool take_busy_tick() noexcept;

// The PaintTracker `renderer` is when a Window is drawing through it on this thread, else null.
[[nodiscard]] PaintTracker* paint_tracker(const Renderer& renderer) noexcept;

// Collects, while it lives on this thread, the focused and focus-visible elements drawn, so their
// focus rings can be drawn over everything once a root is done (render_focus_rings()).
class FocusRingPass
{
public:
    FocusRingPass() noexcept;
    ~FocusRingPass();
    FocusRingPass(const FocusRingPass&)            = delete;
    FocusRingPass& operator=(const FocusRingPass&) = delete;

    // The pass running on this thread, or null.
    [[nodiscard]] static FocusRingPass* active() noexcept;

    std::vector<const Element*> found;

private:
    FocusRingPass* outer_;
};

// Draws the focus ring of each element `pass` found under `root` (drawn with `theme`), through
// the visual transforms and child mappings of the elements above it and clipped to their
// viewports (Element::children_hit_clip()).
void render_focus_rings(Renderer& renderer, const Element& root, const Theme& theme, const FocusRingPass& pass);
struct LayoutAccess;
struct TreeAccess;

// The layout pass Window::layout() is running on this thread, or 0 outside one. Measure and
// arrange results cached in this pass are reused in it whatever the dirty flags say, since the
// flags are only cleared when the pass ends.
[[nodiscard]] std::uint32_t layout_pass() noexcept;

// Makes a fresh pass current on this thread for its lifetime (Window::layout()).
class LayoutPassScope
{
public:
    LayoutPassScope() noexcept;
    ~LayoutPassScope();
    LayoutPassScope(const LayoutPassScope&)            = delete;
    LayoutPassScope& operator=(const LayoutPassScope&) = delete;

private:
    std::uint32_t outer_;
};

#ifndef NDEBUG
// The element whose render_override() is running on this thread, for Element::render()'s re-entry
// assert. Kept out of Element so its layout is the same with and without NDEBUG.
struct RenderGuard
{
    explicit RenderGuard(const Element* element) : outer_(current())
    {
        assert(outer_ != element &&
               "Element::render() re-entered from its own render_override(): an override "
               "must call its base class's render_override(), not render()");
        current() = element;
    }
    ~RenderGuard()
    {
        current() = outer_;
    }
    RenderGuard(const RenderGuard&)            = delete;
    RenderGuard& operator=(const RenderGuard&) = delete;

private:
    static const Element*& current()
    {
        thread_local const Element* element = nullptr;
        return element;
    }

    const Element* outer_;
};
#endif
} // namespace detail

/// A non-owning pointer to an element that goes null when the element is destroyed. It is
/// for something outside the tree that has to reach an element across frames without owning it, and
/// cannot know when the page it sits in is torn down (a ConnectedAnimation's destination). It
/// severs both ways like a Property's Subscription: the element keeps a list of the watches on
/// it, and either side going away unhooks the other. Neither copyable nor movable, since the
/// element holds its address.
///
/// Example:
/// @code
/// // A tip that points at a button on whichever page is showing.
/// class Coachmark
/// {
/// public:
///     void point_at(ui::Element* target)
///     {
///         target_.reset(target);
///     }
///     void tick()
///     {
///         if (ui::Element* target = target_.get()) // null once the page was torn down
///         {
///             place_next_to(target->bounds());
///         }
///     }
///
/// private:
///     void place_next_to(const ui::Rect& anchor); // moves the tip beside `anchor`
///
///     ui::ElementWatch target_;
/// };
/// @endcode
class ElementWatch
{
public:
    /// Watches nothing.
    ElementWatch() = default;
    /// Watches `element` (nullptr: nothing).
    explicit ElementWatch(Element* element)
    {
        reset(element);
    }
    /// Unhooks from the watched element, if it is still alive.
    ~ElementWatch()
    {
        reset(nullptr);
    }
    ElementWatch(const ElementWatch&)            = delete;
    ElementWatch& operator=(const ElementWatch&) = delete;

    /// The watched element, or nullptr once it has been destroyed (or when nothing is watched).
    [[nodiscard]] Element* get() const
    {
        return target_;
    }
    /// Whether the watched element is still alive.
    explicit operator bool() const
    {
        return target_ != nullptr;
    }
    /// Watch `element` instead (nullptr: nothing).
    void reset(Element* element);

private:
    friend struct ElementMotion;
    friend class Element; // retargets the watches when an element is moved
    Element* target_ = nullptr;
};

/// How an element is placed within the (possibly larger) rect its parent affords it on one
/// axis, once margin/min-size are accounted for.
/// `Stretch`, the default, hands the element the whole rect. The other three shrink the element
/// to its own desired extent and position it within the rect. A stack's `child_alignment` uses the
/// same values for all of its children at once, on the axis across the stack (see Stack.hpp).
enum class Alignment
{
    Leading,  ///< left / top
    Center,   ///< centered in the slot
    Trailing, ///< right / bottom
    Stretch,  ///< fills the slot, up to the max size (the default)
};

/// Whether a part a control can show is shown: decided by the control (`Auto`, the default),
/// always, or never. A TeachingTip's tail, a CommandBar's More button and a NavigationShell's back
/// button take it; each field's comment says what `Auto` decides there.
enum class AutoVisibility
{
    Auto,      ///< The control decides.
    Visible,   ///< Always shown.
    Collapsed, ///< Never shown.
};

/// How many items a collection lets the user select, and with which gestures (ListView, TileView,
/// TableView).
enum class SelectionMode
{
    /// Nothing is selectable; a click only invokes or moves the focus cue.
    None,
    /// At most one item; a click or an arrow key selects it.
    Single,
    /// Any number of items: a click or Space toggles one, Shift+click adds a range.
    Multiple,
    /// Any number of items, desktop style: a click selects alone, Ctrl+click toggles, Shift+click
    /// selects a range.
    Extended,
};

/// Where in the viewport a scrolling ancestor puts the element Element::start_bring_into_view()
/// reveals. With no ratio on an axis the ancestor scrolls as little as it can (the element's near
/// edge, or its far edge when it sticks out past the far side). A ratio places the same point of
/// the element and of the viewport together: 0 the near edges, 0.5 the centers, 1 the far edges.
/// The offsets are added to where that lands.
///
/// Example:
/// @code
/// // Center the row vertically in its Scroll, animated.
/// auto row = std::make_unique<ui::TextControl>("Row 42");
/// row->start_bring_into_view({.vertical_alignment_ratio = 0.5f, .animation_desired = true});
/// @endcode
struct BringIntoViewOptions
{
    /// Where across the viewport's width the element lands, 0..1; unset scrolls as little as it can.
    OptionalFloat horizontal_alignment_ratio{};
    /// Where down the viewport's height the element lands, 0..1; unset scrolls as little as it can.
    OptionalFloat vertical_alignment_ratio{};
    /// DIPs added to the horizontal position the ratio (or the smallest scroll) gives.
    float horizontal_offset = 0.0f;
    /// DIPs added to the vertical position the ratio (or the smallest scroll) gives.
    float vertical_offset = 0.0f;
    /// Whether the scroll animates (a Scroll's scroll_to()) or jumps.
    bool animation_desired = false;
    /// The part of the element to reveal, relative to its own top-left; the whole element when unset.
    std::optional<Rect> target_rect{};
};

/// A key combination that invokes an action while focus is inside the element that holds it
/// (Element::add_shortcut()). Unless `on_pressed` returns ui::NotHandled the key goes no
/// further; a NotHandled one goes on to an ancestor's shortcuts and then to the focused element.
///
/// Example:
/// @code
/// auto* editor = window.set_content(
///     std::make_unique<ui::StackControl>(ui::Orientation::Vertical));
/// editor->add_shortcut({.keys = ui::Ctrl + ui::Key::S, .on_pressed = [] { save_document(); }});
/// // Ctrl+S now saves whenever focus is anywhere inside `editor`.
/// @endcode
struct Shortcut
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The key and exactly the modifiers that fire it; an empty key combination never matches.
    KeyCombo keys{};
    /// The action. Returning nothing, or ui::Handled, stops the key; ui::NotHandled lets it go on.
    Callback<Handling()> on_pressed{};
};

/// Which side of its anchor a popup (ComboBox/Menu/Flyout/ToolTip/TeachingTip, see
/// PopupRequest::placement) hangs from. When there isn't room, every popup flips to the opposite
/// side of the same axis instead of covering the anchor. A TeachingTip also tries the other axis.
enum class PopupPlacement
{
    Below, ///< under the anchor (the default)
    Above, ///< over the anchor
    Left,  ///< left of the anchor
    Right, ///< right of the anchor
};

/// How a popup lines up with its anchor along the side PopupPlacement picked: centered, or with
/// the left (top) edges or the right (bottom) edges lined up. A TeachingTip keeps its tail on
/// the target's center, so Leading lets the card run right (or down) from the tail and Trailing
/// left (or up).
enum class PopupPlacementAlignment
{
    Center,   ///< centered on the anchor
    Leading,  ///< left (or top) edges lined up
    Trailing, ///< right (or bottom) edges lined up
};

/// Why a popup (Flyout, MenuFlyout, CommandBarFlyout, TeachingTip) closed.
enum class PopupCloseReason
{
    Programmatic, ///< hide(), `.is_open` set to false, or a TeachingTip that fit nowhere.
    LightDismiss, ///< A click outside it or Escape, with `light_dismiss` on.
    Invoked,      ///< A menu item or command in it ran (MenuFlyout, CommandBarFlyout).
    CloseButton,  ///< A close button: a TeachingTip's (or its corner "X"), an InfoBar's, a CommandBar's More button.
};

/// What a popup's `on_closing` gets: why it is closing. The handler returns ui::Cancel to keep it
/// open.
struct PopupClosingArgs
{
    PopupCloseReason reason = PopupCloseReason::Programmatic; ///< Why it is closing.
};

class Element;

/// How the window draws around a popup's content: the shadow behind it, and the corners and
/// elevation that shadow is cast at.
struct PopupChrome
{
    /// Whether the window casts the theme's popup shadow behind the content. Off for content that
    /// draws all of its own chrome, as a bare Popup's child does.
    bool shadow = true;
    /// The corner radius the shadow is drawn at under a theme, which has to match the radius the
    /// content's own surface takes from that theme. Unset is the theme's `overlay_corner_radius`.
    Callback<float(const Theme& theme)> corner_radius{};
    /// How high the shadow is cast from, in DIPs, on a theme with Fluent shadows. Unset is a
    /// flyout's 32.
    OptionalFloat elevation{};
    /// The rect the shadow is cast behind, given the popup's arranged bounds. Unset is the whole
    /// popup; a surface with a tail or a part still sliding in narrows it.
    Callback<Rect(const Rect& bounds)> shadow_rect{};
};

/// What Element::open_popup() floats above the tree: the content, what it hangs from, and how it
/// behaves. The window places it after every layout, draws it over everything else and routes
/// input inside it to it alone.
///
/// Example:
/// @code
/// // A custom control that shows a list under itself while expanded.
/// class Dropdown : public ui::Element
/// {
/// public:
///     void set_expanded(bool expanded)
///     {
///         if (!expanded)
///         {
///             close_popup();
///             return;
///         }
///         open_popup({
///             .content    = list_.get(), // owned by this control
///             .placement  = ui::PopupPlacement::Below,
///             .on_dismiss = [this](ui::PopupCloseReason) { set_expanded(false); },
///         });
///     }
///
/// private:
///     std::unique_ptr<ui::StackControl> list_ = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// };
/// @endcode
struct PopupRequest
{
    /// The element shown in the popup. The control that opens the popup owns it and keeps it alive
    /// while the popup is open; the window lays it out and draws it, and never owns it.
    Element* content = nullptr;
    /// The element the popup hangs from. Unset is the element that opened it. Its bounds are read
    /// again on every layout, so the popup follows it.
    const Element* anchor = nullptr;
    /// A rect inside `anchor` to hang from instead of its whole bounds, relative to the anchor's
    /// top-left corner (a part of the control, the point a tool tip opened at).
    std::optional<Rect> anchor_rect{};
    /// Which side of the anchor the popup opens on. The window flips it to the opposite side when
    /// there is no room.
    PopupPlacement placement = PopupPlacement::Below;
    /// How the popup lines up with the anchor along that side. Leading by default, as a list
    /// under a box is; a centered popup is measured without the anchor's size as a hint.
    PopupPlacementAlignment placement_alignment = PopupPlacementAlignment::Leading;
    /// Whether a press outside every open popup and its owner, or Escape, dismisses it. A popup
    /// without light dismiss stays open until its owner closes it, and an outside press reaches the
    /// tree under it.
    bool light_dismiss = true;
    /// Whether keyboard focus moves into the popup while it is open: the window scopes the tab
    /// ring to the content, focuses its first stop, and gives focus back when it closes.
    bool takes_focus = false;
    /// The shadow the window draws behind the content.
    PopupChrome chrome{};
    /// Positions the popup itself instead of the anchor and placement rule: measure and arrange
    /// `content` inside a `client`-sized window and return true, or return false to use the rule.
    /// It may close the popup (a surface that fits nowhere).
    Callback<bool(Element& content, Size client, const MeasureContext& context)> place{};
    /// Called when the window dismisses the popup (light dismiss, Escape). The owner closes it,
    /// at once with close_popup() or after an exit animation, and updates its own state. Unset closes
    /// it at once.
    Callback<void(PopupCloseReason reason)> on_dismiss{};
};

/// What a press at a point means to the window around it, for a window whose
/// content stands in for its caption: a `Frame::Borderless` window, or content extended into
/// the title bar. Resize edges depend on DPI frame metrics, so the host classifies those itself
/// before asking the tree. The caption-button values are for a caption the app draws itself:
/// the press still reaches the element under it, and the host tells the OS what is there.
enum class HitRegion
{
    Client,  ///< an ordinary click, delivered to the element
    Caption, ///< the window's caption: drag to move, double-click to maximize, right-click for the system menu
    /// The window icon in a drawn caption: a click opens the system menu, a double-click closes
    /// the window. Handled by the OS, not delivered to the element.
    SystemMenu,
    /// A drawn caption's minimize button. A client click, delivered to the element.
    MinimizeButton,
    /// A drawn caption's maximize or restore button. Delivered to the element like a client
    /// click. Where the system offers snap layouts, hovering it also opens their flyout.
    MaximizeButton,
    /// A drawn caption's close button. A client click, delivered to the element.
    CloseButton,
};

/// What an element asks Window to do with keyboard focus, once, polled on every layout pass
/// through take_focus_request() below.
enum class FocusRequest
{
    /// Nothing to do.
    None,
    /// Move focus to focus_request_root()'s first stop in the ring, remembering what had it.
    Enter,
    /// Give focus back to whatever had it when this element last asked to Enter.
    Restore,
};

/// The base of every node in the UI tree: a control, a panel, a window's content.
///
/// Implement it: derive a control from it and override the hooks it needs. You call a verb
/// (measure(), arrange(), render(), tick(), handle_key()) and override its `<verb>_override()`.
///
/// Every node measures, arranges, renders, takes input and may take focus. The public verbs apply
/// what is common to all elements (margin, explicit and min/max size, alignment, visibility,
/// requested theme, the visual transform and opacity, animations, the focus ring) and then call
/// the protected `*_override()` hook a control implements. measure_override() and
/// render_override() are required; the rest default to doing nothing. Pointer and key input is
/// routed: the window offers an event to the element under the pointer, or the focused one, and
/// then to each parent until a hook returns true. Containers own their children through
/// `std::unique_ptr` and list them through for_each_child(); every tree-wide walk (tick(),
/// clear_dirty(), the focus ring, popups, automation) reaches the children through that, so a
/// container overrides it once instead of forwarding each walk. Setters invalidate through
/// mark_dirty(), which bubbles to the root so the window knows to lay out or redraw.
///
/// Elements are rendered as a platform-independent command stream (Renderer), so a control can be
/// measured, arranged, drawn and driven with input without a window or a GPU.
///
/// Example:
/// @code
/// // A minimal custom control: a color swatch that is clicked with the mouse or Space.
/// class SwatchControl final : public ui::Element
/// {
/// public:
///     explicit SwatchControl(ui::Color color) : color_(color) {}
///
///     void set_color(ui::Color color)
///     {
///         color_ = color;
///         mark_dirty(ui::DirtyFlags::Visual); // redraw, no relayout
///     }
///     void on_click(ui::Callback<void()> handler) { on_click_ = std::move(handler); }
///
///     bool focusable() const override { return true; }
///
/// protected:
///     bool key_override(const ui::KeyEvent& e) override
///     {
///         if (e.key != ui::Key::Space)
///         {
///             return false; // to the parents, the shortcuts and focus moves
///         }
///         if (e.action == ui::KeyAction::Up && on_click_)
///         {
///             on_click_();
///         }
///         return true;
///     }
///     ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
///     {
///         return {32.0f, 32.0f};
///     }
///     void render_override(ui::Renderer& r, const ui::RenderContext&) const override
///     {
///         r.fill_rounded_rect(bounds(), 4.0f, color_);
///     }
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         if (e.action == ui::PointerAction::Down)
///         {
///             pressed_ = true; // the press holds the pointer until its release
///             return true;
///         }
///         if (e.action == ui::PointerAction::Up)
///         {
///             // A click is a press and a release, both on the swatch.
///             const bool was_pressed = std::exchange(pressed_, false);
///             if (was_pressed && bounds().contains(e.position) && on_click_)
///             {
///                 on_click_();
///             }
///             return true;
///         }
///         return false;
///     }
///
/// private:
///     ui::Color            color_;
///     bool                 pressed_ = false;
///     ui::Callback<void()> on_click_;
/// };
///
/// // In a tree: the panel owns it, the caller keeps an observer.
/// auto*   panel  = window.set_content(
///     std::make_unique<ui::StackControl>(ui::Orientation::Vertical));
/// SwatchControl* swatch = panel->add(std::make_unique<SwatchControl>(ui::Color::from_argb(0xFF0078D4)));
/// swatch->set_margin(ui::Thickness{4.0f});
/// swatch->set_horizontal_alignment(ui::Alignment::Center);
/// swatch->set_tool_tip("Accent blue");
/// swatch->on_click([] { /* ... */ });
/// @endcode
class Element
{
public:
    /// Leaves keyboard focus bookkeeping consistent (has_focus_within()) when the focused element
    /// goes away with its subtree.
    virtual ~Element();

    // ---- copy / move ----
    /// Not copyable: a control has a parent link, a stable VisualId and its subscriptions, none of
    /// which can be duplicated. A derived control is movable until it is parented (the move
    /// operations are protected); in a tree it is owned through unique_ptr and never moved.
    Element(const Element&)            = delete;
    Element& operator=(const Element&) = delete;

    // ---- visual identity ----
    /// Assigned once at construction and never reused; the stable key other systems
    /// (VisualTree) use instead of this object's address.
    [[nodiscard]] VisualId visual_id() const
    {
        return visual_id_;
    }

    // ---- sizing ----
    /// DIPs of empty space kept outside the element on each side, taken
    /// from the rect the parent offers before measuring and arranging. Zero by default.
    [[nodiscard]] const Thickness& margin() const
    {
        return margin_;
    }
    /// Changes the margin; invalidates layout.
    void set_margin(const Thickness& margin)
    {
        const auto next = margin;
        if (margin_ == next)
        {
            return; // unchanged: a binding that delivers the same value again costs no layout
        }
        margin_ = next;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The explicit width, nullopt for Auto. Measure and arrange both fold it into the min/max range.
    /// The size wins over the content and min_width() wins over the size. A Stretch-aligned element
    /// that ends up narrower than its slot is centered in it.
    [[nodiscard]] std::optional<float> width() const
    {
        return width_;
    }
    /// Sets the explicit width in DIPs (negative clamps to 0), or nullopt for Auto; invalidates layout.
    void set_width(OptionalFloat value)
    {
        const auto next = value ? std::optional<float>{(std::max)(0.0f, *value)} : std::nullopt;
        if (width_ == next)
        {
            return;
        }
        width_ = next;
        mark_dirty(DirtyFlags::Layout);
    }
    /// The explicit height, as width() is the explicit width; nullopt for Auto.
    [[nodiscard]] std::optional<float> height() const
    {
        return height_;
    }
    /// Sets the explicit height in DIPs (negative clamps to 0), or nullopt for Auto; invalidates
    /// layout.
    void set_height(OptionalFloat value)
    {
        const auto next = value ? std::optional<float>{(std::max)(0.0f, *value)} : std::nullopt;
        if (height_ == next)
        {
            return;
        }
        height_ = next;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The maximum width in DIPs; infinity (the default) is no limit. min_width() wins over it.
    [[nodiscard]] float max_width() const
    {
        return max_width_;
    }
    /// Sets the maximum width in DIPs (negative clamps to 0), or nullopt for no limit; invalidates
    /// layout.
    void set_max_width(OptionalFloat value)
    {
        const auto next = value ? (std::max)(0.0f, *value) : std::numeric_limits<float>::infinity();
        if (max_width_ == next)
        {
            return;
        }
        max_width_ = next;
        mark_dirty(DirtyFlags::Layout);
    }
    /// The maximum height, as max_width() is the maximum width; infinity for no limit.
    [[nodiscard]] float max_height() const
    {
        return max_height_;
    }
    /// Sets the maximum height in DIPs (negative clamps to 0), or nullopt for no limit; invalidates
    /// layout.
    void set_max_height(OptionalFloat value)
    {
        const auto next = value ? (std::max)(0.0f, *value) : std::numeric_limits<float>::infinity();
        if (max_height_ == next)
        {
            return;
        }
        max_height_ = next;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The minimum width in DIPs; 0 (the default) is none. Wins over width() and max_width().
    [[nodiscard]] float min_width() const
    {
        return min_width_;
    }
    /// Sets the minimum width in DIPs (negative clamps to 0), or nullopt for none; invalidates
    /// layout.
    void set_min_width(OptionalFloat value)
    {
        const auto next = value ? (std::max)(0.0f, *value) : 0.0f;
        if (min_width_ == next)
        {
            return;
        }
        min_width_ = next;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The minimum height in DIPs; 0 (the default) is none. Wins over height() and
    /// max_height().
    [[nodiscard]] float min_height() const
    {
        return min_height_;
    }
    /// Sets the minimum height in DIPs (negative clamps to 0), or nullopt for none; invalidates
    /// layout.
    void set_min_height(OptionalFloat value)
    {
        const auto next = value ? (std::max)(0.0f, *value) : 0.0f;
        if (min_height_ == next)
        {
            return;
        }
        min_height_ = next;
        mark_dirty(DirtyFlags::Layout);
    }

    /// Where the element sits across its slot's width;
    /// Alignment::Stretch by default.
    [[nodiscard]] Alignment horizontal_alignment() const
    {
        return h_align_;
    }
    /// Changes the horizontal alignment; invalidates layout.
    void set_horizontal_alignment(Alignment alignment)
    {
        const auto next = alignment;
        if (h_align_ == next)
        {
            return;
        }
        h_align_ = next;
        mark_dirty(DirtyFlags::Arrange);
    }

    /// Where the element sits down its slot's height;
    /// Alignment::Stretch by default.
    [[nodiscard]] Alignment vertical_alignment() const
    {
        return v_align_;
    }
    /// Changes the vertical alignment; invalidates layout.
    void set_vertical_alignment(Alignment alignment)
    {
        const auto next = alignment;
        if (v_align_ == next)
        {
            return;
        }
        v_align_ = next;
        mark_dirty(DirtyFlags::Arrange);
    }

    // ---- visual layer (visibility, opacity, translation, rotation, scale) ----

    /// Whether the element is shown. Collapsing marks layout dirty; see Visibility.
    [[nodiscard]] Visibility visibility() const
    {
        return visibility_;
    }
    /// Shows or collapses the element and its subtree; invalidates layout when it changes.
    void set_visibility(Visibility visibility)
    {
        if (visibility_ != visibility)
        {
            visibility_ = visibility;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// Whether this element and every ancestor is Visible. The tab ring and the popup layer
    /// check this, since a Collapsed container's descendants still answer collect_focusables().
    [[nodiscard]] bool is_effectively_visible() const
    {
        for (const Element* e = this; e != nullptr; e = e->parent_)
        {
            if (e->visibility_ == Visibility::Collapsed)
            {
                return false;
            }
        }
        return true;
    }

    // ---- enabled ----
    /// This element's own enabled state, as set_enabled() left it; true by default. Whether it
    /// takes input also depends on its ancestors: see is_enabled().
    [[nodiscard]] bool enabled() const
    {
        return enabled_;
    }
    /// Enables or disables this element and everything inside it. A disabled element draws its
    /// disabled look, takes no pointer or keyboard input and leaves the tab ring; so does every
    /// element inside a disabled one, whatever its own enabled(). Redraws.
    void set_enabled(bool enabled);
    /// Whether the element takes input right now: its own enabled(), a control's own condition
    /// (a Button's command), and every ancestor's. What a control reads to draw its disabled look.
    [[nodiscard]] bool is_enabled() const
    {
        for (const Element* e = this; e != nullptr; e = e->parent_)
        {
            if (!e->enabled_ || !e->enabled_gate_)
            {
                return false;
            }
        }
        return true;
    }

    /// Move keyboard focus here, or, when this element takes no focus itself (a panel, a view's
    /// host), to the first focus stop inside it. False, and focus stays where it was, when it is
    /// in no window or neither is a stop right now (hidden, disabled). Window::push_focus() also
    /// remembers what had focus, to give it back.
    ///
    /// Example:
    /// @code
    /// ui::Control<ui::TextBox> name;
    /// // ...
    /// name->focus(); // after showing a form, so typing goes into the first field
    /// @endcode
    bool focus();

    // ---- window ----
    /// The window whose tree this element is in, or nullptr while it is in none (built but not
    /// yet placed, or taken out).
    [[nodiscard]] Window* window() const
    {
        const Element* e = this;
        while (e->parent_ != nullptr)
        {
            e = e->parent_;
        }
        return e->window_;
    }

    /// Measure and draw this subtree in the light or dark version of the theme it is handed
    /// (Theme::with_dark_mode), and so with every color the subtree reads from the theme
    /// (background resources, text, control fills). No effect where
    /// the theme has no other mode (the fixed-palette eras, high contrast). Tokens the app edited
    /// on the handed theme that are not colors (corner radii, type, motion, focus-ring widths)
    /// stay as edited; colors are the other mode's. A popup opened from inside is drawn by the
    /// window in the window's theme.
    [[nodiscard]] ElementTheme requested_theme() const
    {
        return requested_theme_;
    }
    /// Changes the requested theme; invalidates layout and redraws the subtree.
    void set_requested_theme(ElementTheme theme)
    {
        if (requested_theme_ != theme)
        {
            requested_theme_ = theme;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }

    /// Whether the element takes pointer input. False keeps pointer input from reaching this element
    /// and everything under it. It still draws and still takes keyboard focus. Frame turns it off on
    /// pages while a navigation transition plays.
    [[nodiscard]] bool is_hit_test_visible() const
    {
        return hit_test_visible_;
    }
    /// Turns pointer input to this subtree on or off; takes effect with the next event.
    void set_hit_test_visible(bool visible)
    {
        hit_test_visible_ = visible;
    }

    /// The opacity, 0..1. The value set, not the one drawn mid-animation (current_opacity()).
    [[nodiscard]] float opacity() const;
    /// Sets the opacity (1 by default), animating when an opacity transition is set. Only redraws;
    /// the layout stays as it is.
    void set_opacity(float opacity);
    /// An offset from the layout position that does not affect layout.
    [[nodiscard]] Vector3 translation() const;
    /// Sets the translation in DIPs, animating when a translation transition is set.
    void set_translation(Vector3 translation);
    /// The rotation in clockwise degrees about center_point().
    [[nodiscard]] float rotation() const;
    /// Sets the rotation in degrees, animating when a rotation transition is set.
    void set_rotation(float degrees);
    /// The scale factors, about center_point().
    [[nodiscard]] Vector3 scale() const;
    /// Sets the scale factors ({1, 1, 1} by default), animating when a scale transition is set.
    void set_scale(Vector3 scale);
    /// The pivot for rotation and scale, relative to the element's top-left corner.
    [[nodiscard]] Vector3 center_point() const;
    /// Sets the pivot for rotation() and scale(), DIPs from the top-left corner.
    void set_center_point(Vector3 center);

    /// The values as drawn this frame: the property, or a running animation or transition's value.
    /// An ExpressionAnimation that reads another element should read these.
    [[nodiscard]] float current_opacity() const;
    /// As current_opacity(), for translation().
    [[nodiscard]] Vector3 current_translation() const;
    /// As current_opacity(), for rotation().
    [[nodiscard]] float current_rotation() const;
    /// As current_opacity(), for scale().
    [[nodiscard]] Vector3 current_scale() const;
    /// As current_opacity(), for center_point().
    [[nodiscard]] Vector3 current_center_point() const;

    /// An implicit transition for set_opacity(). While one is set, every later change to that
    /// property animates from the value on screen (see ScalarTransition and Vector3Transition in
    /// Animation.hpp). nullopt (the default) makes changes immediate.
    void set_opacity_transition(std::optional<ScalarTransition> transition);
    /// As set_opacity_transition(), for set_rotation().
    void set_rotation_transition(std::optional<ScalarTransition> transition);
    /// As set_opacity_transition(), for set_translation().
    void set_translation_transition(std::optional<Vector3Transition> transition);
    /// As set_opacity_transition(), for set_scale().
    void set_scale_transition(std::optional<Vector3Transition> transition);
    /// The transition set_opacity() animates through, or nullopt for immediate changes.
    [[nodiscard]] std::optional<ScalarTransition> opacity_transition() const;
    /// The transition set_rotation() animates through, or nullopt.
    [[nodiscard]] std::optional<ScalarTransition> rotation_transition() const;
    /// The transition set_translation() animates through, or nullopt.
    [[nodiscard]] std::optional<Vector3Transition> translation_transition() const;
    /// The transition set_scale() animates through, or nullopt.
    [[nodiscard]] std::optional<Vector3Transition> scale_transition() const;

    /// Run `animation` on one of this element's visual properties. It replaces whatever runs on any
    /// of the same components. A finished DoubleAnimation or KeyFrameAnimation that holds its end
    /// (the default) writes its last value into the property, and so does a finished spring. An
    /// ExpressionAnimation runs until stop_animation().
    void start_animation(AnimatedProperty property, Animation animation);
    /// Stop the animation on `property`: the components go back to the property's own value.
    void stop_animation(AnimatedProperty property);
    /// Whether an animation or implicit transition is running on any component of `property`.
    [[nodiscard]] bool is_animating(AnimatedProperty property) const;

    /// The transitions this element runs itself (Repositioning when
    /// layout moves it, Entrance when it is first laid out, ...).
    [[nodiscard]] const TransitionList& transitions() const;
    /// Replaces this element's own transitions.
    void set_transitions(TransitionList transitions);
    /// The transitions every child of this element runs, as if
    /// each had them itself. Only meaningful on a container.
    [[nodiscard]] const TransitionList& children_transitions() const;
    /// Replaces the transitions every child runs.
    void set_children_transitions(TransitionList transitions);

    /// The transition target (Animation.hpp's TransitionTargetState) as drawn this frame.
    [[nodiscard]] const TransitionTargetState& transition_target() const;
    /// Drive the transition target directly, for an owner that runs its own item transitions
    /// (ListView's) and needs no storyboard on the element.
    void set_transition_target(const TransitionTargetState& state);
    /// Play `storyboard` on the transition target, replacing one already running; `on_completed`
    /// fires once it ends (not when it is replaced), after the target is back at identity. An
    /// empty storyboard has nothing to play: it resets the target and calls `on_completed` at
    /// once, inside this call.
    void run_transition(TransitionStoryboard storyboard, Callback<void()> on_completed = {});
    /// Whether a transition storyboard is running.
    [[nodiscard]] bool transition_running() const;

    /// The size from the last arrange pass.
    [[nodiscard]] Size actual_size() const
    {
        return bounds_.size();
    }
    /// The layout position relative to the parent's (bounds() are
    /// window-absolute here).
    [[nodiscard]] Point actual_offset() const
    {
        return parent_ != nullptr ? Point{bounds_.x - parent_->bounds_.x, bounds_.y - parent_->bounds_.y}
                                  : Point{bounds_.x, bounds_.y};
    }

    /// The transform this element is drawn through, in window coordinates: scale then rotation
    /// about center_point(), then translation, then the transition target, about the layout
    /// position. Identity for an element whose visual layer was never touched.
    [[nodiscard]] Matrix visual_transform() const;

    // ---- reactivity ----
    /// Take ownership of a Subscription so that its lifetime is exactly this node's
    /// lifetime. The tree is single-owner and destroyed hierarchically, so a node's
    /// subscriptions die with it and a Property can never dispatch into freed memory. A Property
    /// dying first is handled inside Subscription itself (core/Property.hpp).
    ///
    /// Called by the spec layer's build() for every reactive field; an app writing
    /// imperatively may call it directly to tie a hand-made observer to a node's lifetime.
    void add_subscription(Subscription subscription)
    {
        check_thread();
        subscriptions_.push_back(std::move(subscription));
    }
    /// How many bindings this node holds: a spec that binds N fields produces N subscriptions.
    /// Nothing in the framework branches on it.
    [[nodiscard]] std::size_t subscription_count() const
    {
        return subscriptions_.size();
    }
    /// Drop every binding on this node, leaving whatever values were last written in place. Call it
    /// to stop tracking the bound properties and drive the control by hand from then on.
    void clear_subscriptions()
    {
        subscriptions_.clear();
    }

    // ---- shortcuts ----
    /// Add a shortcut scoped to this element. Window offers every key press to the
    /// shortcuts of the focused element and then of each of its ancestors, innermost first,
    /// before the focused element's own handle_key() sees it, so a Tabs control's Ctrl+T works wherever
    /// focus is inside it. A shortcut added here only fires while focus is in its element.
    void add_shortcut(Shortcut shortcut)
    {
        extras().shortcuts.push_back(std::move(shortcut));
    }
    /// Offers `e` to this element's shortcuts; true when one handled it. Controls with keys
    /// of their own that must work from anywhere inside them (Tabs control's Ctrl+Tab) override this,
    /// calling the base first.
    virtual bool handle_shortcut(const KeyEvent& e)
    {
        if (!extras_)
        {
            return false;
        }
        for (Shortcut& a : extras_->shortcuts)
        {
            if (a.keys.matches(e) && a.on_pressed && detail::handled(a.on_pressed(), true))
            {
                return true;
            }
        }
        return false;
    }
    /// The shortcuts that fire wherever focus is. The ones a command surface carries are of this
    /// kind (a MenuBar's Ctrl+S, a MenuFlyout item's Delete, an AppBarButton's Ctrl+N), so those
    /// controls answer here. Window
    /// offers a key that no scoped shortcut took to every element in the window, in tree order,
    /// when the focused element and its parents did not handle the key themselves
    /// (key_override()). Default: none.
    virtual bool handle_global_shortcut(const KeyEvent& e)
    {
        (void)e;
        return false;
    }

    // ---- tooltip ----
    /// A plain-text tool tip on any element. Window shows it beside the pointer once
    /// the pointer has rested on the element for 800 ms, and hides it on a click, a key or the
    /// pointer moving off. ToolTip (ui/dialogs/ToolTip.hpp) stays the way to give a tip arbitrary
    /// content or its own placement.
    void set_tool_tip(std::string text)
    {
        check_thread();
        if (text.empty() && !extras_)
        {
            return;
        }
        extras().tool_tip = std::move(text);
    }
    /// The tip for window point `p`, which Window asks for as the pointer moves: tool_tip() unless
    /// the control shows a different tip per part (a TableView cell). A change of text while the
    /// pointer stays on the control closes the open tip and starts the wait again.
    [[nodiscard]] virtual std::string tool_tip_at(Point p) const
    {
        (void)p;
        return tool_tip();
    }
    /// The text Window shows; a control whose tip is derived (an AppBarButton's "Save (Ctrl+S)")
    /// overrides this.
    [[nodiscard]] virtual std::string tool_tip() const
    {
        return extras_ ? extras_->tool_tip : std::string{};
    }

    // ---- pointer cursor ----
    /// The cursor shown while the pointer is over this element (or over a descendant that asks for
    /// none). nullopt, the default, asks for nothing, so an ancestor's cursor or the control's own
    /// cursor_at() decides, and failing everything the pointer is an arrow.
    [[nodiscard]] std::optional<CursorShape> cursor() const
    {
        return extras_ ? extras_->cursor : std::nullopt;
    }
    /// Sets or clears (nullopt) the cursor; Window picks it up the next time the pointer moves.
    void set_cursor(std::optional<CursorShape> shape)
    {
        if (!shape && !extras_)
        {
            return;
        }
        extras().cursor = shape;
    }
    /// The control's own cursor at `p` (window-client DIPs, inside bounds()) when set_cursor() has
    /// set none: IBeam over a TextBox's text, Hand over a hyperlink,
    /// a resize arrow over a splitter. nullopt (the default) leaves it to the ancestors. Window
    /// asks the innermost element under the pointer first, then each parent in turn, and the first
    /// answer wins.
    [[nodiscard]] virtual std::optional<CursorShape> cursor_at(Point /*p*/) const
    {
        return std::nullopt;
    }

    // ---- accessibility ----
    /// The caller-supplied accessible name, description and decorative flag (a spec's
    /// `.accessibility`). automation_name() prefers its name over the control's own text; empty
    /// by default, which is right for any control with visible text.
    [[nodiscard]] const Accessibility& accessibility() const
    {
        return extras_ ? extras_->accessibility : no_accessibility();
    }
    /// Replaces the accessibility information; read by the next automation query.
    void set_accessibility(Accessibility accessibility)
    {
        check_thread();
        extras().accessibility = std::move(accessibility);
    }

    // ---- automation ----
    /// What screen readers and other assistive tools see of this node: the role, name, help text
    /// and children here, and what a client can do with the node through the providers
    /// automation_providers() lists (ui/Automation.hpp).
    ///
    /// automation_role() is the role this node reports; Group by default.
    [[nodiscard]] virtual AutomationRole automation_role() const
    {
        return AutomationRole::Group;
    }

    /// The accessible name: accessibility().name when the caller set one, otherwise the control's
    /// own visible text. Not virtual, like measure(). A control overrides
    /// automation_name_fallback() instead.
    [[nodiscard]] std::string automation_name() const
    {
        const Accessibility& a = accessibility();
        return a.name.empty() ? automation_name_fallback() : a.name;
    }
    /// The accessible help text a screen reader offers after the name: accessibility().description
    /// when the caller set one, otherwise automation_help_text_fallback().
    [[nodiscard]] std::string automation_help_text() const
    {
        const Accessibility& a = accessibility();
        return a.description.empty() ? automation_help_text_fallback() : a.description;
    }
    /// Whether screen readers hear the value as valid: false while automation_invalid() says
    /// the control holds input it reports as wrong.
    [[nodiscard]] bool automation_valid() const
    {
        return !automation_invalid();
    }

    /// The children an automation client sees under this node, immediate children only. A client
    /// walks the tree one level at a time, and the parent comes from parent(). Defaults to every
    /// child for_each_child() lists. Override it only to show a client something else: the
    /// children in a different order, or without parts that are only decoration. A composite that
    /// is one automation node lists nothing.
    virtual void automation_children(std::vector<Element*>& out) const;
    /// While this element has keyboard focus, the node inside it that has it as automation sees
    /// it: the row, tab or menu item a control moves its own cursor over with the arrow keys.
    /// Screen readers announce that node. nullptr (the default) is this element itself.
    [[nodiscard]] virtual Element* automation_focus() const
    {
        return nullptr;
    }

    /// True only for NativeControl (ui/hosting/NativeControl.hpp). Its child HWND answers
    /// automation clients itself, so the automation tree (ui/AutomationTree.hpp) leaves it out and
    /// treats it as childless, which keeps it from being described twice.
    [[nodiscard]] virtual bool automation_is_native_host() const
    {
        return false;
    }

    /// The automation node between this element and its child `child`, for a container whose
    /// automation tree has a level its element tree lacks (a table's cell around a template cell
    /// the table owns). nullptr, the default, makes this element the child's automation parent.
    [[nodiscard]] virtual Element* automation_parent_of(const Element& child) const
    {
        (void)child;
        return nullptr;
    }

    // ---- layout ----
    /// Measure the desired size within `available`. Unbounded (+infinity) on an axis means no
    /// limit; 0 is no room. The result is always finite.
    /// Applies the margin and the size limits, then calls measure_override() for the
    /// control-specific work. Subclasses implement that hook and don't override measure().
    /// Asked again with the same size, measurer and theme while nothing at or below the element
    /// was marked DirtyFlags::Measure, it returns what it returned last time without calling
    /// measure_override(): a measure_override() must depend on nothing else, or invalidate_measure()
    /// whenever that something changes.
    /// The context's measurer and theme must outlive the next measure(): a virtualizing container
    /// re-measures from arrange() and render() with the ones it was last measured with.
    [[nodiscard]] Size measure(Size available, const MeasureContext& context) const
    {
        const TextMeasurer& measurer = context.measurer;
        const Theme&        handed   = context.theme;
        if (visibility_ == Visibility::Collapsed)
        {
            desired_       = Size{};
            measure_valid_ = false;
            return Size{};
        }
        const std::uint32_t pass = detail::layout_pass();
        if (measure_valid_ && available == measured_available_ && &measurer == measured_with_ &&
            &handed == measured_theme_ && handed.generation() == measured_theme_generation_ &&
            ((pass != 0 && measured_pass_ == pass) || !any(dirty_ & DirtyFlags::Measure)))
        {
            return measured_result_;
        }
        // Asked for two different sizes in one pass (a Grid's auto track, a Stack's unbounded
        // first look): the parent's answer may hang on both, so a relayout from here does not
        // stop at this element (Window::layout()).
        measured_twice_ = pass != 0 && measured_pass_ == pass && measure_valid_ && !(available == measured_available_);
        measure_valid_  = true; // before measure_override(), so one that invalidates itself stays invalid
        arrange_valid_  = false;
        const Theme& theme      = requested_theme_ == ElementTheme::Default ? handed : scoped_theme(handed);
        const float  reserved   = margin_.horizontal();
        const float  reserved_v = margin_.vertical();
        // inf - reserved stays inf; a finite axis that runs out is 0, never unbounded.
        const Size shrunk{(std::max)(0.0f, available.width - reserved), (std::max)(0.0f, available.height - reserved_v)};
        // The offer is pulled into [min, max] (an unbounded axis
        // with a finite max is offered the max), and the result is floored at min, capped at max.
        const Range w = width_range();
        const Range h = height_range();
        const Size  offered{constrain_offer(shrunk.width, w), constrain_offer(shrunk.height, h)};
        assert(offered.width >= 0.0f && offered.height >= 0.0f); // also false for NaN
        detail::count(detail::PerfCounter::Measures);
        Size core = measure_override(offered, &theme == &handed ? context : context.with_theme(theme));
        assert(std::isfinite(core.width) && std::isfinite(core.height));
        core.width                 = (std::min)((std::max)(core.width, w.min), w.max);
        core.height                = (std::min)((std::max)(core.height, h.min), h.max);
        desired_                   = core; // consumed by arrange()'s alignment math below
        measured_available_        = available;
        measured_with_             = &measurer;
        measured_theme_            = &handed;
        measured_theme_generation_ = handed.generation();
        measured_pass_             = pass;
        measured_result_           = Size{core.width + reserved, core.height + reserved_v};
        return measured_result_;
    }

    /// measure() with a context of `measurer` and `theme`, for measuring a control on its own.
    /// The theme defaults to Theme::default_light(), as render()'s does.
    [[nodiscard]] Size measure(Size available, const TextMeasurer& measurer, const Theme& theme = Theme::default_light()) const
    {
        return measure(available, MeasureContext{measurer, theme});
    }

    /// Whether this element's measure() may run on a layout worker thread while its siblings are
    /// measured on others (ParallelLayout). Default false, so an element nobody
    /// checked is always measured on the UI thread.
    ///
    /// Answer true only when measure_override() touches nothing but this element's own state, the
    /// children it lists in for_each_child() (each of which answers for itself), the measurer
    /// and the theme: it builds no children, reads or writes no Property, subscribes to
    /// nothing, invalidates nothing, and uses no cache or static shared with other elements. A
    /// part it measures without listing it in for_each_child() makes the answer false. The
    /// answer is inherited, so a subclass whose measure_override() does more overrides it back.
    [[nodiscard]] virtual bool parallel_measure_safe() const
    {
        return false;
    }

    /// Place the element at its final position and size within `final_rect`. Applies margin and
    /// alignment, then calls arrange_override() to position any children. The default
    /// arrange_override() does nothing, since most leaves need only `bounds()`. measure() is always
    /// called before arrange(). Given the rect it was last given,
    /// with nothing at or below it measured or marked DirtyFlags::Arrange since, it does nothing.
    void arrange(const Rect& final_rect)
    {
        if (visibility_ == Visibility::Collapsed)
        {
            bounds_        = Rect{final_rect.x, final_rect.y, 0.0f, 0.0f};
            arrange_valid_ = false;
            return;
        }
        const std::uint32_t pass = detail::layout_pass();
        if (arrange_valid_ && final_rect == arranged_rect_ &&
            ((pass != 0 && arranged_pass_ == pass) || !any(dirty_ & DirtyFlags::Layout)))
        {
            return;
        }
        arranged_rect_   = final_rect;
        arranged_pass_   = pass;
        arrange_valid_   = true; // before arrange_override(), so one that invalidates itself stays invalid
        const Rect inner = final_rect.inset(margin_);

        // A stretched element still stops at its max, and is then centered in the rest of its slot.
        const float width  = h_align_ == Alignment::Stretch
                                 ? (std::min)(inner.width, (std::max)(desired_.width, width_range().max))
                                 : (std::min)(desired_.width, inner.width);
        const float height = v_align_ == Alignment::Stretch
                                 ? (std::min)(inner.height, (std::max)(desired_.height, height_range().max))
                                 : (std::min)(desired_.height, inner.height);

        float x = inner.x;
        if (h_align_ == Alignment::Center || (h_align_ == Alignment::Stretch && width < inner.width))
        {
            x += (inner.width - width) * 0.5f;
        }
        else if (h_align_ == Alignment::Trailing)
        {
            x += inner.width - width;
        }

        float y = inner.y;
        if (v_align_ == Alignment::Center || (v_align_ == Alignment::Stretch && height < inner.height))
        {
            y += (inner.height - height) * 0.5f;
        }
        else if (v_align_ == Alignment::Trailing)
        {
            y += inner.height - height;
        }

        bounds_ = Rect{x, y, width, height};
        // Transitions watch layout: the first arrange is the element's Load, a moved one its
        // Layout trigger. Only elements with transitions (own or the parent's) pay for the check.
        if (motion_ || (parent_ != nullptr && parent_->has_children_transitions_))
        {
            note_arranged();
        }
        laid_out_ = true;
        detail::count(detail::PerfCounter::Arranges);
        arrange_override(bounds_, arrange_context());
    }

    /// The rect the last arrange() gave this element, in window-client DIPs (not relative to the
    /// parent), after margin and alignment; excludes the margin. Empty
    /// before the first layout, and zero-sized while Collapsed.
    [[nodiscard]] const Rect& bounds() const
    {
        return bounds_;
    }

    // ---- non-client hit testing ----
    /// Classify `p` (window-client DIPs, inside bounds()) for Window::hit_region_at(). Each
    /// element answers for its whole subtree. The default passes the question to the topmost
    /// child under the point, of those for_each_child() lists, and answers Client where there is
    /// none, so an ordinary tree is Client everywhere and only a subtree that opts in drags the
    /// window. TitleBarControl answers Caption for its own background. DragAreaControl answers
    /// Caption for everything but the focusable controls inside it.
    [[nodiscard]] virtual HitRegion hit_region(Point p) const
    {
        const Element* topmost = nullptr;
        for_each_child(
            [&topmost, p](Element& child)
            {
                if (child.bounds().contains(p))
                {
                    topmost = &child; // drawn last is on top
                }
            });
        return topmost != nullptr ? topmost->hit_region(p) : HitRegion::Client;
    }

    /// Whether a press on this element, inside a TitleBar's content, drags the window. nullopt
    /// (the default) lets the TitleBar decide: an enabled focusable control takes the press,
    /// anything else drags. true drags even over a control (and, on a container, lets its controls
    /// drag too unless they say false). false never drags, over this element or anything inside
    /// it. Read on every hit test, so a change takes effect at once.
    [[nodiscard]] std::optional<bool> title_bar_drag_region() const
    {
        return extras_ ? extras_->title_bar_drag_region : std::nullopt;
    }
    /// Sets or clears (nullopt) the drag-region override; read by the next hit test.
    void set_title_bar_drag_region(std::optional<bool> value)
    {
        if (!value && !extras_)
        {
            return;
        }
        extras().title_bar_drag_region = value;
    }

    // ---- render ----
    /// Draw this element: nothing while Collapsed or fully transparent, else render_override()
    /// through the visual transform and opacity (visual_transform(), current_opacity() times the
    /// transition target's). Emits no extra command for an element whose visual layer is at rest,
    /// so a still tree renders exactly what render_override() alone would. A container draws each
    /// child through this, handing on the context it got.
    void render(Renderer& renderer, const RenderContext& context) const
    {
        if (visibility_ == Visibility::Collapsed)
        {
            return;
        }
        // Drawn by a Window: remember where, and skip a subtree a clipped frame cannot reach.
        if (detail::PaintTracker* tracker = detail::paint_tracker(renderer))
        {
            render_tracked(*tracker, context);
            return;
        }
        const RenderContext here = requested_theme_ == ElementTheme::Default
                                       ? context
                                       : context.with_theme(scoped_theme(context.theme));
#ifndef NDEBUG
        const detail::RenderGuard guard(this);
#endif
        detail::count(detail::PerfCounter::Renders);
        if (!motion_)
        {
            render_own(renderer, here);
            return;
        }
        render_with_visual_layer(renderer, here);
    }
    /// Draw this element under `theme` (Theme::default_light() by default): a control drawn on
    /// its own, or the root of a tree.
    void render(Renderer& renderer, const Theme& theme = Theme::default_light()) const
    {
        render(renderer, RenderContext{theme});
    }

    // ---- input ----
    /// Deliver a pointer event to this element and everything inside it. It is hit-tested once:
    /// the topmost Visible, hit-test visible (is_hit_test_visible()) and enabled element under the
    /// pointer gets it in pointer_override(), with its position mapped into that element's layout
    /// coordinates (through every visual_transform() and map_to_children() on the way), then each
    /// parent up to this one, until one returns true. A press that something handled captures the
    /// pointer for that element and its parents until the release, so they get the moves and the
    /// release wherever the pointer goes. An element the pointer moves off gets a Leave (once the
    /// capture is over, if it holds it), and on_pointer_entered()/on_pointer_exited() are raised.
    /// Window calls this on its content; a test can call it on any element, which then routes
    /// within its own subtree. Returns whether an element handled the event.
    bool handle_pointer(const PointerEvent& e);

    /// The handler for the pointer moving onto or off this element (a
    /// press holds neither back; the pointer leaving the window exits). Hit where the element is
    /// drawn, as its input is.
    void on_pointer_entered(Callback<void()> handler);
    /// The handler for the pointer leaving the element; see on_pointer_entered().
    void on_pointer_exited(Callback<void()> handler);
    /// The handler for keyboard focus arriving on this element, or on anything inside it, from
    /// outside it. Moving focus between two elements inside it raises nothing here. Window raises
    /// it once the focused element has been told (set_focused()).
    void on_got_focus(Callback<void()> handler);
    /// The handler for keyboard focus leaving this element and everything inside it; see
    /// on_got_focus().
    void on_lost_focus(Callback<void()> handler);

    /// A wheel turn, which Window routes through this instead of handle_pointer(): hit-tested like
    /// a pointer event, offered to the element under the pointer in wheel_override() and then to
    /// each parent until one returns true, so the innermost scrollable region under the pointer
    /// scrolls and the page behind it does not. Returns whether an element reacted to it.
    bool handle_wheel(const PointerEvent& e);
    /// A key press or release. Window delivers it to the focused element and then to each
    /// parent in turn until one handles it; what nothing handles goes on to the shortcuts, a
    /// dialog's default button and arrow-key focus moves. Offers a press to on_key_down() first,
    /// then calls key_override(); true when either took it.
    bool handle_key(const KeyEvent& e)
    {
        if (e.action == KeyAction::Down && extras_ && extras_->key_down && detail::handled(extras_->key_down(e), false))
        {
            return true;
        }
        return key_override(e);
    }
    /// The handler for a key pressed (or repeating) while focus is on this element or inside it,
    /// for app code: an Escape that closes a view, arrow keys that page through it. It sees the
    /// key before this element's own handling and after that of the elements inside it that
    /// have focus. ui::Handled stops the key there; ui::NotHandled, or returning nothing, lets it
    /// go on to this element and then to the ones around it. One handler per element; setting
    /// another replaces it.
    ///
    /// Example:
    /// @code
    /// ui::Property<bool> details_open{true};
    /// auto               view = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
    /// view->on_key_down([&](const ui::KeyEvent& e) {
    ///     if (e.key != ui::Key::Escape) return ui::NotHandled;
    ///     details_open = false;
    ///     return ui::Handled;
    /// });
    /// @endcode
    void on_key_down(Callback<Handling(const KeyEvent&)> handler)
    {
        check_thread();
        extras().key_down = std::move(handler);
    }
    /// The handler for a wheel turn over this element or one inside it, for app code: Ctrl+wheel
    /// to zoom. It sees the turn before this element scrolls and after the elements inside it
    /// under the pointer had theirs; `e.position` is in this element's coordinates, and
    /// `e.wheel_delta` is positive when the wheel turns toward the user (scroll down). ui::Handled
    /// stops the turn there; ui::NotHandled, or returning nothing, lets it go on. One handler per
    /// element; setting another replaces it.
    ///
    /// Example:
    /// @code
    /// ui::Property<float> zoom{1.0f};
    /// auto                tiles = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
    /// tiles->on_wheel([&](const ui::PointerEvent& e) {
    ///     if (!e.modifiers.ctrl) return ui::NotHandled;
    ///     zoom = std::clamp(zoom.get() + (e.wheel_delta < 0 ? 0.1f : -0.1f), 0.5f, 3.0f); // up zooms in
    ///     return ui::Handled;
    /// });
    /// @endcode
    void on_wheel(Callback<Handling(const PointerEvent&)> handler)
    {
        check_thread();
        extras().wheel = std::move(handler);
    }
    /// Offers a wheel turn to on_wheel(), then to wheel_override(); true when either took it.
    /// What the wheel dispatch calls for each element under the pointer.
    bool raise_wheel(const PointerEvent& e)
    {
        if (extras_ && extras_->wheel && detail::handled(extras_->wheel(e), false))
        {
            return true;
        }
        return wheel_override(e);
    }
    /// Typed text, routed like a key (focused element first, then its parents). Calls
    /// text_override() and returns its answer.
    bool handle_text(const TextInputEvent& e)
    {
        return text_override(e);
    }
    /// handle_text() with the text alone.
    bool handle_text(std::string_view text)
    {
        return handle_text(TextInputEvent{std::string(text)});
    }

    // ---- focus ----
    /// Whether this element can take keyboard focus (a leaf control opts in).
    [[nodiscard]] virtual bool focusable() const
    {
        return false;
    }
    /// Whether Tab and Shift+Tab reach this element while it has focus, instead of moving focus
    /// (a text box that indents). Ctrl+Tab never moves focus either way. Default: false.
    [[nodiscard]] virtual bool takes_tab_key() const
    {
        return false;
    }
    /// Alt or F10 alone (`letter` 0), or Alt with a letter nothing else used, asks for the menus.
    /// The window offers it to each visible, enabled element in tree order until one takes it: a
    /// menu bar takes keyboard focus (and opens the menu whose header starts with `letter`) and
    /// returns true. Default: false.
    virtual bool take_menu_access(char32_t letter)
    {
        (void)letter;
        return false;
    }
    /// Whether this element has keyboard focus: the window's focused element, or a part a composite
    /// control draws as focused on its behalf (a ComboBox's text field). Kept by the window.
    [[nodiscard]] bool is_focused() const
    {
        return focused_;
    }
    /// Whether a press at `p` (window DIPs) gives this focusable element focus. Window asks it of
    /// every stop in the ring and focuses the innermost that says yes, so a stop inside another
    /// wins. The default is the whole of bounds(); a control that is one stop for only part of
    /// what it covers answers for that part, or a press on non-focusable space inside the rest
    /// would focus it (NavigationShell: its menu, not the page it hosts).
    [[nodiscard]] virtual bool takes_focus_at(Point p) const
    {
        return bounds().contains(p);
    }

    /// Whether a focused element draws its focus ring: true when the keyboard moved focus, false
    /// after a click gave it. Window writes it; a control checks it next to its own focused
    /// state when it renders. True by default, so a control focused without a Window draws its
    /// ring.
    [[nodiscard]] bool focus_visible() const
    {
        return focus_visible_;
    }

    /// Whether keyboard focus is on this element or on one inside it. A repeater checks it before
    /// clearing an element (a focused element stays realized). Kept by Window through
    /// note_keyboard_focus(); an element destroyed while focused takes its part of the count with it.
    [[nodiscard]] bool has_focus_within() const
    {
        return focus_within_ > 0;
    }
    /// Window's side of has_focus_within(): this element gained or lost keyboard focus.
    void note_keyboard_focus(bool focused);

    /// Append focusable elements in tab order, so the Window can build a flat focus ring over the
    /// whole tree. The default pushes this element when focusable(), then recurses into every
    /// child for_each_child() lists. Override it when the ring differs from the children: a
    /// composite that is one tab stop pushes only itself, a focus scope stops recursing (see
    /// collect_scope_focusables()), a pager leaves out its hidden pages.
    virtual void collect_focusables(std::vector<Element*>& out);

    /// The set of choices this control is one of, or nullptr (the default). Focusable controls
    /// that return the same pointer act as one group for the keyboard: Tab stops once on the
    /// group, on the member whose SelectionItemProvider says it is selected, or else the first. The
    /// arrow keys move between the members in tree order, wrapping, and select the one they reach
    /// (SelectionItemProvider::select_item()). Ctrl+arrow only moves. Automation clients see each
    /// member's position in the set. A RadioButton bound with ui::selects() returns the property it
    /// was bound to.
    [[nodiscard]] virtual const void* selection_group() const
    {
        return nullptr;
    }
    /// The element Window focuses when this becomes the open modal, or nullptr for the first
    /// stop in its tab ring. DialogControl picks something in its content first, else its default
    /// button.
    [[nodiscard]] virtual Element* default_focus()
    {
        return nullptr;
    }

    // ---- focus scopes ----
    /// Whether this element is a focus scope. By default a container adds its focusable
    /// descendants to the window's one flat tab ring (collect_focusables()), so Tab walks the
    /// whole tree leaf by leaf. A focus scope is one tab stop instead. Enter steps into it, Tab
    /// then cycles only its own inner ring, and Escape steps back out onto the container
    /// (Window::enter_focus_scope() and exit_focus_scope()). A navigation pane of fifteen
    /// collapsible groups that are scopes is fifteen Tab stops wide instead of eighty.
    ///
    /// Off by default, and set per instance: the same Expander can be a grouping box in one
    /// layout and a navigable group in another. A control can serve as a scope only if it
    /// overrides collect_scope_focusables() (and scope_step() for scopes that navigate items).
    /// Setting the flag on a control that overrides neither has no effect, since Window doesn't
    /// enter a scope with an empty inner ring.
    [[nodiscard]] bool focus_scope() const
    {
        return focus_scope_;
    }
    /// Makes this element a focus scope (or an ordinary, transparent container again); redraws.
    void set_focus_scope(bool scope)
    {
        focus_scope_ = scope;
        mark_dirty(DirtyFlags::Visual);
    }


    // ---- bring into view ----
    /// Ask every scrolling ancestor to reveal this element, innermost first. Window calls
    /// it whenever focus lands somewhere, because a tab ring that walks into a scrolled-off
    /// region would otherwise park focus on something the user cannot see.
    void bring_into_view()
    {
        const std::optional<Rect> requested = bring_into_view_area();
        if (!requested)
        {
            return;
        }
        Rect area = *requested;
        for (Element* ancestor = parent_; ancestor != nullptr; ancestor = ancestor->parent())
        {
            area = ancestor->reveal_child(area, BringIntoViewOptions{});
        }
    }

    /// Ask every scrolling ancestor to reveal this element (or
    /// `options.target_rect` of it), placed as the options say. Unlike bring_into_view() the whole
    /// element is revealed, focus ring or not, and whatever bring_into_view_area() says.
    void start_bring_into_view(const BringIntoViewOptions& options = {})
    {
        Rect area = bounds_;
        if (options.target_rect)
        {
            area = Rect{bounds_.x + options.target_rect->x,
                        bounds_.y + options.target_rect->y,
                        options.target_rect->width,
                        options.target_rect->height};
        }
        for (Element* ancestor = parent_; ancestor != nullptr; ancestor = ancestor->parent())
        {
            area = ancestor->reveal_child(area, options);
            // Only the innermost scroller applies the ratios; the ones outside it only have to
            // show what it now shows.
            if (ancestor->scroll_info())
            {
                BringIntoViewOptions outer;
                outer.animation_desired = options.animation_desired;
                for (Element* next = ancestor->parent(); next != nullptr; next = next->parent())
                {
                    area = next->reveal_child(area, outer);
                }
                return;
            }
        }
    }

    /// What the scrolling ancestors (and the window itself, the root's bounds) leave in view around
    /// this element, in arranged coordinates, not clipped to the
    /// element's own bounds. Empty when an ancestor shows nothing; a huge rect when nothing above
    /// the element clips it at all (an element with no parent).
    [[nodiscard]] Rect effective_viewport() const;

    /// Where text typed into this element goes, while it has focus: the caret, in arranged
    /// coordinates, or nullopt when it takes no text. The platform puts an input method's
    /// candidate window and the emoji panel there (Window::caret_bounds()). A control that hands
    /// its typing to an inner text box answers with that box's.
    [[nodiscard]] virtual std::optional<Rect> caret_bounds() const
    {
        return std::nullopt;
    }

    /// What bring_into_view() reveals when focus lands here (arranged coordinates), or nullopt to
    /// leave every scrolling ancestor where it is. The element decides, the scroller follows.
    ///
    /// Override it when the whole element is not what should come into view. A grid that
    /// draws all its cards itself has bounds as tall as its content, so revealing them jumps
    /// the viewport to the top on every focus gain; it answers with the current card's rect
    /// instead, or nullopt when there is none.
    [[nodiscard]] virtual std::optional<Rect> bring_into_view_area() const
    {
        return bounds_;
    }

    /// Reveal `area` (arranged coordinates) and return where it sits after scrolling, so the next
    /// ancestor up works with the new position. That makes a scroller nested in a scroller work
    /// without a layout pass in between. `options` places it
    /// (start_bring_into_view()); bring_into_view() passes the defaults, the smallest scroll.
    /// Default: nothing to reveal, geometry unchanged. Scroll is the only override.
    virtual Rect reveal_child(const Rect& area, const BringIntoViewOptions& options)
    {
        (void)options;
        return area;
    }

    // ---- scroll source ----
    /// What a scrolling element tells something that follows its scrolling, such as a
    /// Parallax.
    struct ScrollInfo
    {
        Point offset{};    ///< how far the view is scrolled into the content (zoomed DIPs)
        Size  viewport{};  ///< the view's size
        Size  extent{};    ///< the content's size, unzoomed
        float zoom = 1.0f; ///< the zoom factor; 1 unzoomed
        /// How far past either end the content can be pulled: a tenth of the viewport for a
        /// plain scroller, a whole viewport for a zoomable one.
        Size out_of_bounds_pan{};
        /// The element that is the content, whose top-left is the content's origin; nullptr when
        /// the content is not one element (a list's realized rows).
        const Element* content = nullptr;
    };
    /// This element's scrolling, or nullopt for one that does not scroll (the default). Scroll,
    /// VirtualList and ListView answer.
    [[nodiscard]] virtual std::optional<ScrollInfo> scroll_info() const
    {
        return std::nullopt;
    }

    // ---- tree linkage ----
    /// The container this element sits in (non-owning), or nullptr for a root or an element not
    /// yet added anywhere.
    [[nodiscard]] Element* parent() const
    {
        return parent_;
    }

    /// Call `visit` once for each child this element owns, in order (back to front where they
    /// overlap). The one override a container needs beyond measure, arrange and render. The
    /// default of every tree-wide walk recurses through it (collect_focusables(),
    /// the popup walk, clear_dirty(), build_visual_tree(), sync_composition(),
    /// sync_native(), tick() and automation_children()), so a custom container that lists its
    /// children here gets focus, popups, dirty clearing, ticks, composition, native controls and
    /// automation right without forwarding any of them. Default: no children, a leaf.
    ///
    /// List the children the walks should reach, normally every one this element owns. A walk
    /// that must see a different set overrides that walk alone: a composite that is one tab stop
    /// and one automation node, a split view leaving its closed pane out of focus and popups. A
    /// control that parks parts (an unselected page, a hidden button) may leave them out here
    /// instead and clear them in clear_dirty_override() (and tick them in tick_override()), so a
    /// parked part never stays dirty.
    ///
    /// Const so the const walks can use it, though it hands out mutable children: they are owned
    /// objects of their own, not part of this element's state. A tick can run a handler that adds
    /// or removes children, so a container that allows that walks its list by index.
    ///
    /// Example:
    /// @code
    /// class Row : public ui::Element
    /// {
    ///     void for_each_child(ui::ChildVisitor visit) const override
    ///     {
    ///         for (const auto& child : children_)
    ///         {
    ///             visit(*child);
    ///         }
    ///     }
    ///
    ///     std::vector<std::unique_ptr<ui::Element>> children_;
    /// };
    /// @endcode
    virtual void for_each_child(ChildVisitor visit) const
    {
        (void)visit;
    }

    // ---- invalidation ----
    /// Whether any of `flags` is set on this node, for this node or, since flags bubble up,
    /// for something below it. A new element starts fully dirty.
    [[nodiscard]] bool is_dirty(DirtyFlags flags = DirtyFlags::All) const
    {
        return any(dirty_ & flags);
    }
    /// Every dirty flag currently set on this node.
    [[nodiscard]] DirtyFlags dirty_flags() const
    {
        return dirty_;
    }


    // ---- on screen ----
    /// Whether the last frame the Window measured drew some of this element inside the client
    /// area: false once it is scrolled out of view, clipped away, collapsed, fully transparent or
    /// no longer drawn at all. Only an element that called set_screen_tracking() is told; for any
    /// other element, and for one no frame has measured yet, this is true.
    [[nodiscard]] bool on_screen() const
    {
        return on_screen_;
    }
    /// Called after a frame in which on_screen() changed, for an element that called
    /// set_screen_tracking(). The frame that brings an element back into view calls it with
    /// true, so a control that paused its clock off screen restarts it here (request_tick_in()
    /// works from it). The default does nothing.
    virtual void on_screen_changed(bool on_screen)
    {
        (void)on_screen;
    }

    // ---- time ----
    /// What Window::tick() calls on the root with the time elapsed since the last tick. Elements
    /// never read a clock themselves. Advances this element's own animations and transitions (its
    /// visual layer), runs tick_override(), then ticks every child for_each_child() lists. Not
    /// virtual, so the children are always ticked. A handler run by the tick may remove elements.
    /// They are kept alive until the tick returns, and a child removed before its turn is skipped.
    void tick(Duration delta);

    // ---- native controls ----
    /// Called by Window after layout/input changes, with sync_composition()'s cadence. The
    /// default only recurses into the children (for_each_child()); NativeControl
    /// (winui/ui/hosting/NativeControl.hpp) overrides it to place its child window.
    virtual void sync_native(NativeHost& host);

    // ---- spell checking ----
    /// Hands the focused element the spell checker. This is not called tree-wide. Window calls it
    /// from set_focus(), right before set_focused(true), and before every key or character, so a
    /// field is checked the moment it gains focus. A field keeps the pointer after losing
    /// focus (its squiggles stay, and it still re-checks when the checker's generation() moves
    /// on). A field that was never focused is never checked. TextBox overrides it. Composites
    /// with an internal TextBox (ComboBox/NumberBox/AutoSuggestBox) do not forward it, since
    /// none of them hold prose.
    virtual void sync_spell_checker(SpellChecker&)
    {
    }

    // ---- context menu ----
    /// Window::handle_context_menu() calls this on the focused element when the user
    /// right-clicks or presses Shift+F10/the Menu key, after focusing whatever focusable element
    /// sits under the pointer, as a pointer Down would. `position` is the right-click point
    /// (window-client DIPs), or nullopt for a keyboard request, where the element picks its own
    /// anchor (TextBox: its caret). `host` is only valid for this call, so show a menu through
    /// it here or not at all. Returns whether a menu was shown; default: none.
    virtual bool handle_context_menu(std::optional<Point> /*position*/, ContextMenuHost& /*host*/)
    {
        return false;
    }
    /// The handler for a right-click, Shift+F10 or the Menu key on this element or
    /// one inside it. For a right-click Window asks the innermost element under the pointer and
    /// then each ancestor, focusable or not; then, and for the keys, the focused element and each
    /// ancestor, innermost first, ahead of handle_context_menu(). The handler is the target asked
    /// to act: unless it returns ui::NotHandled it has shown something (a MenuFlyout's show_at(), a
    /// CommandBarFlyout) and the request goes no further. `position` is as handle_context_menu()'s.
    /// One handler per element; setting another replaces it.
    void on_context_requested(Callback<Handling(std::optional<Point>)> handler)
    {
        check_thread();
        extras().context_requested = std::move(handler);
    }
    /// Runs the handler set above, if any; whether it took the request. A wrapper that stands in
    /// for the element it wraps in the tab ring (a flyout around its anchor) asks that element too.
    virtual bool raise_context_requested(std::optional<Point> position)
    {
        return extras_ && extras_->context_requested && detail::handled(extras_->context_requested(position), true);
    }

    // ---- drag and drop (ui/DragDrop.hpp) ----
    /// Whether this element is a drop target, getting drag_enter()/drag_over()/drag_leave()/drop()
    /// while a drag (DragDrop::start()) is over it.
    [[nodiscard]] bool allow_drop() const
    {
        return allow_drop_;
    }
    /// Makes the element a drop target (or not); takes effect for the next drag movement.
    void set_allow_drop(bool allow)
    {
        allow_drop_ = allow;
    }
    /// The drop events. drag_over() is asked first on the innermost drop target under the pointer,
    /// then on its drop targets up the tree until one accepts; every element asked has heard
    /// drag_enter() first and hears drag_leave() once the drag is no longer its business. The one
    /// that accepted gets drop(). A control overrides them; the defaults run the handlers set with
    /// on_drag_enter()/on_drag_over()/on_drag_leave()/on_drop().
    ///
    /// drag_enter(): the drag has come over this element; drag_over() follows at once.
    virtual void drag_enter(const DragEventArgs& e);
    /// The drag is over this element: answer what a drop here would do. Asked when it arrives
    /// and at every move. The default runs the on_drag_over() handler, and refuses without one.
    virtual DragResponse drag_over(const DragEventArgs& e);
    /// The drag left this element, was canceled over it, or was dropped on another target.
    virtual void drag_leave(const DragEventArgs& e);
    /// The pointer was released over this element while its drag_over() accepted. What the drop
    /// did is the operation it accepted.
    virtual void drop(const DragEventArgs& e);
    /// The handler the default drag_enter() runs; one per element, setting another replaces it.
    void on_drag_enter(Callback<void(const DragEventArgs&)> handler);
    /// The handler the default drag_over() runs, which answers for the element.
    void on_drag_over(Callback<DragResponse(const DragEventArgs&)> handler);
    /// The handler the default drag_leave() runs.
    void on_drag_leave(Callback<void(const DragEventArgs&)> handler);
    /// The handler the default drop() runs.
    void on_drop(Callback<void(const DragEventArgs&)> handler);
    /// A context request inside a subtree that is not in the tab ring (an item's content, drawn by an
    /// items control that is the one tab stop): raised on the innermost visual child of `root` under
    /// `position` (or on `root` for a keyboard request) and bubbled to `root`.
    /// Returns whether a handler took it.
    static bool raise_context_requested_in(Element& root, std::optional<Point> position);

    /// A drag this element started has ended. `result` is what the target did
    /// (None when the drag was canceled or dropped nowhere).
    virtual void drag_completed(DataPackageOperation result)
    {
        (void)result;
    }

    /// How far this element's own open or close fade has come, 0 (hidden) to 1 (fully shown), as
    /// for a Flyout's open and close fade or a DialogControl's open fade. The control advances it
    /// in tick_override(). The window draws everything the element shows at this opacity: a popup
    /// owner's floating content, or a modal's scrim together with its content, so one fade covers
    /// both. Default 1: fully visible, no fade running.
    [[nodiscard]] virtual float transition_opacity() const
    {
        return 1.0f;
    }

    // ---- visual tree ----
    /// Add this node, every child for_each_child() lists under it, and then what
    /// build_visual_tree_override() adds, into `tree` under `parent_id` (InvalidVisualId for the
    /// root). Called by Window::layout() after arrange(), so `bounds()` reflects the just-computed
    /// layout. Not virtual, so the children are always reached.
    void build_visual_tree(VisualTree& tree, VisualId parent_id) const;

protected:
    /// Constructs an element with a visual_id() of its own, fully dirty and in no tree.
    Element() = default;

    // ---- automation providers ----
    /// Lists what a client can do with this node: set `out`'s field for each pattern this
    /// control implements now to `this`, through the provider interface it inherits privately
    /// (ui/Automation.hpp). A pattern that only applies in some states is an `if`. `out` arrives
    /// empty, and nothing is listed by default.
    ///
    /// Example:
    /// @code
    /// class Chip final : public ui::Element, private ui::InvokeProvider
    /// {
    /// protected:
    ///     void automation_providers(ui::AutomationProviders& out) override
    ///     {
    ///         out.invoke = this;
    ///     }
    ///
    /// private:
    ///     bool invoke() override
    ///     {
    ///         on_click_();
    ///         return true;
    ///     }
    ///     ui::Callback<void()> on_click_;
    /// };
    /// @endcode
    virtual void automation_providers(AutomationProviders& out)
    {
        (void)out;
    }

    // ---- popup ----
    /// Floats `request.content` above the tree, hung from `request.anchor` (this element by
    /// default), or updates the popup this element has open. Each element has at most one popup;
    /// a control that shows several uses several elements. The window places it after every
    /// layout, draws it with its shadow over everything else, routes input inside it to it alone,
    /// and on a press outside it or Escape calls `request.on_dismiss`. It closes with close_popup()
    /// or when this element is destroyed. Calling it again while open, from arrange_override() for
    /// an anchor that moves, lays the popup out again only when something other than the callbacks
    /// changed.
    void open_popup(PopupRequest request);
    /// Closes the popup open_popup() opened. Nothing when none is open.
    void close_popup();
    /// Whether this element has a popup open.
    [[nodiscard]] bool popup_open() const noexcept
    {
        return extras_ && extras_->popup;
    }

    /// Moves a not-yet-parented element (a factory returning a control by value); see the copy
    /// operations' comment. Never move an element that is already in a tree (asserted). The new
    /// element gets a visual_id() of its own and starts fully dirty; the ElementWatch objects on
    /// the source follow it here. What the moved-from element is left with is only fit to be
    /// destroyed.
    Element(Element&& other) noexcept;
    /// Deleted: an element has an identity (its visual id, the watches on it, the handlers that
    /// captured it), so assigning one over another has no sensible meaning.
    Element& operator=(Element&&) = delete;

    // ---- sizing hooks ----
    /// Implement these instead of measure() and arrange(). The base class has already applied
    /// margin, size limits and alignment by the time these run. measure_override() is pure
    /// (every concrete Element must size itself); arrange_override() defaults to a no-op since
    /// most leaves need nothing beyond having bounds() set, which the base already did.
    [[nodiscard]] virtual Size measure_override(Size available, const MeasureContext& context) const = 0;
    /// Position children within the rect arrange() settled on (also in bounds(), window DIPs).
    /// Default: nothing, for a leaf.
    virtual void arrange_override(const Rect&, const ArrangeContext&)
    {
    }

    // ---- render / input hooks ----
    /// What the control draws, in layout coordinates; render() has already applied the visual
    /// layer. A container draws its children through their render(), never their render_override().
    virtual void render_override(Renderer& renderer, const RenderContext& context) const = 0;
    /// A pointer event for this element: the element under the pointer, one inside it that did
    /// not handle the event, or the element holding the capture. The position is in layout
    /// coordinates (handle_pointer()). Return true when the event did something here, and it goes
    /// no further up; a handled press also captures the pointer until its release. A control that
    /// acts on a press (a click, a toggle, a caret placed, a drag started) returns true for the
    /// press and its release, so a list row or a card around it does not act on the same click.
    /// Default: false.
    virtual bool pointer_override(const PointerEvent& e)
    {
        (void)e;
        return false;
    }
    /// A pointer event that an element inside this one handled (its pointer_override() returned
    /// true), so it never reached this element's pointer_override(). Called on each element above
    /// the one that handled it, innermost first, with the position in this element's layout
    /// coordinates. A container that has to see every press inside it, handled or not, overrides
    /// it: a swipe that may turn a press on a button into a drag, a table that commits an open
    /// edit when a check box in another row is pressed. Default: nothing.
    virtual void handled_pointer_override(const PointerEvent& e)
    {
        (void)e;
    }
    /// A wheel turn over this element (handle_wheel()). Return true when it scrolled something
    /// here. Default: an element with no children hands it to pointer_override() and returns
    /// false; one with children returns false.
    virtual bool wheel_override(const PointerEvent& e);

    // ---- hit testing ----
    /// The children `p` (in this element's layout coordinates) is tested against, topmost first.
    /// Default: what for_each_child() lists, last first. A container that draws in another order
    /// (Panel's z-index), a control with parts for_each_child() does not list, or one whose parts
    /// cover others in places (a pane drawn over the content) lists them here.
    virtual void hit_test_children(Point p, std::vector<Element*>& out) const;
    /// A point in this element's layout coordinates, mapped into its children's: a control that
    /// scrolls, zooms or scales its content maps it the way the content is drawn. Default: the
    /// point unchanged.
    [[nodiscard]] virtual Point map_to_children(Point p) const
    {
        return p;
    }
    /// Where the children can be hit, in this element's layout coordinates, or nullopt for
    /// anywhere (the default): a scrolling control returns its viewport, so content scrolled out
    /// of view is not hit.
    [[nodiscard]] virtual std::optional<Rect> children_hit_clip() const
    {
        return std::nullopt;
    }
    /// Whether the pointer passes through this element and everything in it to what is behind,
    /// as if it were not there: a wrapper with nothing visible in it (ContentHost while its child
    /// is missing or collapsed). Default: false, so an element is hit anywhere in its bounds.
    [[nodiscard]] virtual bool hit_test_transparent() const
    {
        return false;
    }
    /// Whether a part of this element drawn over its children is at `p` (a scroll bar over the
    /// content), so the element is hit there instead of a child. Default: false.
    [[nodiscard]] virtual bool covers_children_at(Point p) const
    {
        (void)p;
        return false;
    }
    /// Capture the pointer of the event being handled: keep receiving its events, wherever it
    /// goes, until release_pointer() or the release of the button. A handled press does this by
    /// itself. Only while handling a pointer event.
    void capture_pointer();
    /// End a capture_pointer() or the capture a press took.
    void release_pointer();

    // ---- walk hooks ----
    /// This element's own part of tick(): a clock of its own (ToolTip's hover delay, an
    /// animation's phase, a caret blink). Runs after the visual layer has advanced and before the
    /// children are ticked, which tick() does by itself. Tick only parts that for_each_child()
    /// does not list here. Default: nothing.
    virtual void tick_override(Duration delta)
    {
        (void)delta;
    }
    /// This element's own part of clear_dirty(): clear any part it owns but leaves out of
    /// for_each_child() (a parked page, a hidden button), through that part's clear_dirty().
    /// Default: nothing.
    virtual void clear_dirty_override(DirtyFlags flags)
    {
        (void)flags;
    }
    /// This element's own part of build_visual_tree(): add any part it owns but leaves out of
    /// for_each_child() through that part's build_visual_tree(tree, visual_id()). Default: nothing.
    virtual void build_visual_tree_override(VisualTree& tree) const
    {
        (void)tree;
    }
    // ---- focus scopes and traps (called by the window) ----
    /// The inner tab ring, built when this scope is entered: a scope's counterpart to
    /// collect_focusables(). A container that becomes a scope must stop recursing in
    /// collect_focusables() and recurse here instead (Expander is the worked example), since a
    /// child in both rings would be reachable without ever entering.
    ///
    /// Three meaningful answers:
    ///   - several elements: an ordinary container scope. Tab cycles them, Escape leaves.
    ///   - `this` alone: an item-navigating scope. The control's items are not all elements
    ///     (ListView creates its rows on demand and destroys them as they scroll out), so focus
    ///     stays on the control and Tab is routed to scope_step() below.
    ///   - nothing: there is nothing inside worth entering. Window doesn't enter, and the key
    ///     that would have entered goes to this element's own handle_key(). That is how Enter
    ///     expands a collapsed Expander that is marked as a scope.
    ///
    /// The one walk that does not default to recursing through for_each_child(): being a scope
    /// is something a control opts into, and a default inner ring would make every container
    /// enterable while its children still sat in the outer ring too.
    virtual void collect_scope_focusables(std::vector<Element*>&)
    {
    }

    /// Move an item-navigating scope's own inner cursor one step: `delta` is +1 for Tab and
    /// -1 for Shift+Tab. Wrap and return true to keep focus inside the scope (Escape is then the
    /// way out, as for an ordinary container scope). Return false to let the step fall out,
    /// popping the scope and leaving focus on this control. Never called for a scope whose inner
    /// ring is real elements, since Window walks that ring itself.
    virtual bool scope_step(int)
    {
        return false;
    }

    /// Called by Window as this scope is entered (before its inner ring takes focus) and
    /// again as it is left, so a control can show and hide whatever inner cursor it draws.
    /// A container scope whose inner ring is real Elements usually needs nothing here, since the
    /// focused child draws its own focus ring, as it would anywhere else.
    virtual void set_scope_entered(bool)
    {
    }

    // ---- in-tree focus traps ----
    /// For a part of the main tree that takes focus when it opens, as SidePane's
    /// light-dismissable pane does. It holds the tab ring while open (by answering
    /// collect_focusables() with the pane alone) and hands focus back when it closes.
    /// Returning a request consumes it; the default never asks.
    /// Escape reaches it like any key the focused element inside it leaves alone (key_override()).
    [[nodiscard]] virtual FocusRequest take_focus_request()
    {
        return FocusRequest::None;
    }
    /// The subtree an Enter request focuses into; `this` by default.
    [[nodiscard]] virtual Element* focus_request_root()
    {
        return this;
    }

    // ---- tree bookkeeping for containers ----
    /// Record `parent` as the container `part` sits in (nullptr: none), when a container adds a
    /// child or a part of its own. Dirty flags bubble up this link, and the new parent chain
    /// takes on the part's dirty flags and keyboard focus (has_focus_within()) while the old
    /// one lets go of the focus, so a container that adopts a dirty part is laid out again
    /// without marking itself. Not an ownership transfer: the container already holds the
    /// part's unique_ptr.
    static void set_parent_of(Element& part, Element* parent)
    {
        part.set_parent(parent);
    }
    /// Clear `flags` on `part` and its subtree, for a clear_dirty_override() clearing a part
    /// this element leaves out of for_each_child().
    static void clear_dirty_of(Element& part, DirtyFlags flags = DirtyFlags::All)
    {
        part.clear_dirty(flags);
    }
    /// Show `part` focused or not, for a composite that draws one of its parts as focused while
    /// it holds the focus itself (a ComboBox's text field). Hand focus_visible() on with
    /// set_focus_visible_of() too, or the part keeps drawing its ring after a click.
    static void set_focused_of(Element& part, bool focused)
    {
        part.set_focused(focused);
    }
    /// Hand the focus-visible state on to a part; see set_focused_of().
    static void set_focus_visible_of(Element& part, bool visible)
    {
        part.set_focus_visible(visible);
    }

    // ---- focus ring ----
    /// Whether the window draws this element's focus ring while it is focusable(), is_focused()
    /// and focus_visible(). True by default, so a focusable control gets the ring by doing
    /// nothing. The window draws it after everything else in the window, or in the popup the
    /// element is in, clipped only by the scrolling viewports around the element, and layout
    /// never makes room for it. The theme decides its shape and whether it sits just outside or
    /// just inside focus_ring_rect().
    /// Return false only for a control that shows focus in a shape of its own (a text field's
    /// accent border) or not at all.
    [[nodiscard]] virtual bool draws_focus_ring() const
    {
        return true;
    }
    /// What the ring goes around, in window DIPs: bounds() by default. A control whose ring goes
    /// around one part of it (a slider's thumb, a list's current row) returns that part. `theme`
    /// is the one the element is drawn with, for a ring placed differently per theme.
    [[nodiscard]] virtual Rect focus_ring_rect(const Theme& theme) const
    {
        (void)theme;
        return bounds();
    }
    /// Whether the ring goes inside focus_ring_rect() in every era, its outer edge on the rect.
    /// False by default, which leaves it to the theme. An item inside a viewport (a list row, a
    /// tab, a menu item) returns true, as does a control in a tight spot, so a neighbor or the
    /// viewport edge never cuts its ring.
    [[nodiscard]] virtual bool focus_ring_inside() const
    {
        return false;
    }
    /// The ring's corner radius: the theme's control radius by default.
    [[nodiscard]] virtual float focus_ring_corner_radius(const Theme& theme) const
    {
        return theme.corner_radius;
    }

    // ---- keyboard hooks ----
    /// A key for this element: the focused one, or one inside it that did not handle it. Return
    /// true when the key did something here, and it goes no further; false lets it bubble on to
    /// the parent and then to the shortcuts, a dialog's default button and arrow-key focus
    /// moves. Default: false.
    virtual bool key_override(const KeyEvent& e)
    {
        (void)e;
        return false;
    }
    /// Typed text for this element, routed like key_override(). Default: false.
    virtual bool text_override(const TextInputEvent& e)
    {
        (void)e;
        return false;
    }
    /// The usual answer for a focused control's key_override(): an arrow when `arrow` (the
    /// control uses that arrow), Enter when `enter`, a shortcut-like key (Ctrl, Alt, Delete,
    /// Insert, a function key) when `shortcut`, never Tab or Escape, and any other key (typing,
    /// Space, Home, PageDown) always, since a focused control owns its plain keys.
    [[nodiscard]] static bool key_handled(const KeyEvent& e, bool arrow, bool enter, bool shortcut);

    // ---- state hooks ----
    /// Keyboard focus arrived (true) or left (false); is_focused() already says so. The element
    /// is redrawn either way. No-op by default.
    virtual void focus_changed(bool focused)
    {
        (void)focused;
    }
    /// focus_visible() changed; the element is redrawn either way. No-op by default.
    virtual void focus_visible_changed(bool visible)
    {
        (void)visible;
    }
    /// is_enabled() flipped, through set_enabled() here or on an ancestor, or a move to another
    /// parent. Drop any press or hover state. The element is redrawn either way. No-op by default.
    virtual void enabled_changed()
    {
    }
    /// A condition of the control's own that is ANDed into is_enabled() (a Button whose command
    /// may not run). True by default.
    void set_enabled_gate(bool open);
    /// The element joined a window's tree: window() is now that window. Called for the root a
    /// window takes and for everything inside it, and for a subtree added to a tree already in a
    /// window. No-op by default.
    virtual void attached()
    {
    }
    /// The element left a window's tree (moved out of it). No-op by default.
    virtual void detached()
    {
    }

    // ---- automation hook ----
    /// automation_name()'s fallback when accessibility().name is empty. Default: no inherent
    /// name, correct for controls with no visible text of their own (Slider, ProgressBar), which
    /// need an explicit accessibility().name from the caller. A control
    /// with its own visible text (Button, CheckBox, TextControl, ...) overrides this to return
    /// it.
    [[nodiscard]] virtual std::string automation_name_fallback() const
    {
        return {};
    }
    /// automation_help_text()'s fallback when accessibility().description is empty. Default:
    /// none. An input with a `description` under it returns that text, or its `error` while set.
    [[nodiscard]] virtual std::string automation_help_text_fallback() const
    {
        return {};
    }
    /// True while the control holds input it reports as wrong (an input's `error` is set); screen
    /// readers say so. Default: false.
    [[nodiscard]] virtual bool automation_invalid() const
    {
        return false;
    }

    /// Mark this node dirty and bubble the newly set bits up the parent chain, so an ancestor's
    /// flags show that something below it changed without walking the subtree. Bubbling stops at
    /// the first ancestor that already has the bit, since every ancestor of a dirty node is
    /// dirty in the same way.
    ///
    /// Every setter goes through here, so this is where a build with thread checks catches a
    /// setter called on an element under a Window from another thread than the window's. Leave
    /// `where` defaulted: it names the setter that called this.
    void mark_dirty(DirtyFlags flags, std::source_location where = std::source_location::current())
    {
#if LIBWINUI_THREAD_CHECKS
        check_tree_thread(where);
#else
        (void)where;
#endif
        detail::note_dirty_mark();
        if (any(flags & DirtyFlags::Measure))
        {
            measure_valid_ = false;
        }
        if (any(flags & DirtyFlags::Layout))
        {
            arrange_valid_ = false;
        }
        for (Element* e = this; e != nullptr; e = e->parent_)
        {
            const DirtyFlags newly = flags & ~e->dirty_;
            e->dirty_ |= flags;
            if (!any(newly))
            {
                break;
            }
            flags = newly;
        }
    }

    /// This element's size has to be measured again (and it arranged again): mark_dirty() with
    /// DirtyFlags::Layout. For a setter whose value changes the element's desired size.
    void invalidate_measure(std::source_location where = std::source_location::current())
    {
        mark_dirty(DirtyFlags::Layout, where);
    }

    /// This element has to be arranged again, its size has not changed: mark_dirty() with
    /// DirtyFlags::Arrange. For a setter that only moves the element or its children within the
    /// space they already have (an alignment, a scroll position that is laid out).
    void invalidate_arrange(std::source_location where = std::source_location::current())
    {
        mark_dirty(DirtyFlags::Arrange, where);
    }

    /// Keep the window ticking: for an element that waits for time to pass without changing
    /// anything in the meantime (a caret between blinks, a delay before something opens). A
    /// window stops ticking a few seconds after its last tick that changed something, so a
    /// longer quiet wait calls this from tick_override() for as long as it lasts. A wait of known
    /// length is cheaper with request_tick_in().
    void request_tick()
    {
        detail::note_tick_request();
    }

    /// Have the window tick again `delay` from now (0 or less: at once), for an element whose next
    /// change has a known time (the next frame of an animated image). Unlike request_tick() it
    /// does not keep the window busy: an otherwise idle window sleeps until then and wakes only
    /// for it (Window::next_wake()). Call it from tick_override() or on_screen_changed(). It holds
    /// for one tick, so an element that still waits asks again from its next tick_override(), which
    /// may come sooner. A hidden or minimized window does not wake for it.
    ///
    /// What the same tick_override() changes with mark_dirty() is that step's change (a caret blink,
    /// an image's next frame): it is drawn, and it does not make the window busy either. A
    /// tick_override() that also animates something calls request_tick() as well, or the window
    /// waits for the requested time before its next tick.
    void request_tick_in(Duration delay)
    {
        detail::note_wake_request(delay.count());
    }

    /// Have the Window report on_screen() for this element and call on_screen_changed() when it
    /// changes, from the frames it measures from now on. Off by default, and free while off. A
    /// control turns it on while its drawing moves on its own (an animated image), to pause
    /// while nobody can see it. Turning it off makes on_screen() true again.
    void set_screen_tracking(bool on);

    /// mark_dirty()'s thread check without the dirtying, for a setter that changes nothing drawn
    /// (an event handler, a tip): reports a call from another thread than the one of the Window
    /// this element is under. Leave `where` defaulted: it names the setter that called this.
    void check_thread(std::source_location where = std::source_location::current()) const
    {
#if LIBWINUI_THREAD_CHECKS
        check_tree_thread(where);
#else
        (void)where;
#endif
    }


    /// For a container whose child just started an AddRemoveTransition exit. In the next
    /// arrange the children it moves close the gap only once that exit has played, and a child
    /// added in the same pass fades in after them, so neither slides over the one still
    /// leaving. A RepositionTransition on those children gives way to it for that pass.
    void note_child_deleted();

private:
    Element* parent_ = nullptr; // the owning container, non-owning; see parent()

    // ---- visual layer (Element.cpp) ----
    [[nodiscard]] Point to_local(Point p) const;       // through visual_transform(), inverted
    void                raise_pointer_over(bool over); // pointer entered and exited, once each
    friend struct detail::PointerDispatch;
    friend void    detail::render_focus_rings(Renderer&, const Element&, const Theme&, const detail::FocusRingPass&);
    void           render_with_visual_layer(Renderer& renderer, const RenderContext& context) const;
    void           render_tracked(detail::PaintTracker& tracker, const RenderContext& handed) const;
    void           advance_motion(Duration delta);
    void           note_arranged();
    ElementMotion& motion();
    [[nodiscard]] const Theme&   scoped_theme(const Theme& handed) const; // requested_theme()'s
    [[nodiscard]] ArrangeContext arrange_context() const;                 // the last measure()'s
    void                         mark_subtree_dirty(DirtyFlags flags);    // a new theme, from Window
    friend class ElementWatch;
    // Window's side of on_got_focus()/on_lost_focus(): whether either is set, and raising one.
    [[nodiscard]] bool has_focus_handler() const noexcept;
    void               raise_focus_event(bool got);
    // Window's side of the popup: the open request, and a dismissal (light dismiss, Escape).
    [[nodiscard]] PopupRequest* popup_request() const noexcept
    {
        return extras_ ? extras_->popup.get() : nullptr;
    }
    void dismiss_popup(PopupCloseReason reason);
    // Runs the request's place callback, if it has one; false without one.
    bool place_popup(Element& content, Size client, const MeasureContext& context);
    // The elements the window asks for popups, tool tips, global shortcuts and focus requests:
    // this one, then every child for_each_child() lists. A built-in control that parks parts (an
    // unselected page, a closed pane) leaves them out, or adds parts it does not list.
    virtual void collect_live(std::vector<Element*>& out);
    friend class Window;             // sets tree_thread_ on its roots, clears the dirty flags after a frame
    friend class ControlHeader;      // takes the header off the top of bounds_
    friend class ControlDescription; // and the description off the bottom
    friend struct detail::LayoutAccess;
    friend struct detail::TreeAccess;
    friend struct testing::ElementAccess;

    // ---- composition, for the built-in controls (detail::Compositor is the backend's) ----
    /// Called by Window after layout, input or focus changes, so a control can hand its state
    /// transition animations to the compositor instead of redrawing them frame by frame:
    /// sync_composition_override() for this element, then every child for_each_child()
    /// lists. Not virtual, so the children are always reached.
    void sync_composition(detail::Compositor& compositor, const Theme& theme);

    // ---- compositor layers ----
    /// How many compositor layers this element wants for part of its drawing right now: 0 (the
    /// default) draws everything into the window's frame. A layer is a visual with content of its
    /// own that the compositor animates while the UI thread draws nothing, for a motion that never
    /// stops (a spinner). The Window decides each frame whether the layers run: only with a
    /// compositor that supports them, animations on, nothing drawn over the element later in the
    /// frame, and no scale, rotation or opacity on the element or above it. While they run,
    /// layers_composed() is true and render_override() leaves out what they show. They sit above the
    /// element's own drawing and its children, and the Window removes them when the element
    /// leaves the frame or this count drops to 0.
    [[nodiscard]] virtual std::size_t layer_count() const
    {
        return 0;
    }
    /// Where layer `index` sits and what clips it, in this element's coordinates (bounds()'s
    /// space). A layer whose animation moves its content past `bounds` needs a `clip`.
    [[nodiscard]] virtual detail::LayerPlacement layer_placement(std::size_t index) const;
    /// Draws layer `index`'s content at rest, in this element's coordinates, with primitives only.
    /// The Window records it every frame and repaints the layer only when it changes.
    virtual void render_layer(std::size_t index, Renderer& renderer, const Theme& theme) const
    {
        (void)index;
        (void)renderer;
        (void)theme;
    }
    /// Starts the layers' animations (Compositor::animate_layer()), one id per layer in index
    /// order. Called when the layers start running and whenever one changes size. `elapsed` is
    /// how long they have run so far, so a cycle continues where it was.
    virtual void animate_layers(detail::Compositor& compositor, std::span<const VisualId> layers, Duration elapsed)
    {
        (void)compositor;
        (void)layers;
        (void)elapsed;
    }
    /// The layers stopped after running `elapsed` on the compositor. A control with its own
    /// phase clock adds the time to it, so its own drawing picks up where the compositor was.
    virtual void layers_stopped(Duration elapsed)
    {
        (void)elapsed;
    }
    /// Whether the compositor shows this element's layers: render_override() leaves out what they
    /// show, and tick_override() has nothing to redraw for them. Meaningful while layer_count() is
    /// above 0.
    [[nodiscard]] bool layers_composed() const
    {
        return layers_composed_;
    }

    /// This element's own part of sync_composition(): drive its compositor-owned state
    /// transitions (Button's hover and press). Sync only parts that for_each_child() does not
    /// list here. Default: nothing.
    virtual void sync_composition_override(detail::Compositor& compositor, const Theme& theme)
    {
        (void)compositor;
        (void)theme;
    }


    // ---- tree linkage (set_parent_of() and clear_dirty_of() for containers) ----
    // Set by a container when a child or part is added; see set_parent_of().
    void set_parent(Element* parent);
    // Takes this element and its subtree off every Property it is bound to, for retire(), and
    // keeps the handlers until the elements are destroyed.
    void unlink_subscriptions() noexcept;
    // Window's side of is_focused() and focus_visible(): records, redraws, runs the hook.
    void set_focused(bool focused);
    void set_focus_visible(bool visible);
    // render_override(), noting a focused element for the window's focus ring pass.
    void render_own(Renderer& renderer, const RenderContext& context) const;
    // enabled_changed() on this element and every descendant whose is_enabled() is now `now`.
    void notify_enabled(bool now);
    // attached()/detached() on this element and its subtree.
    void notify_window(bool attached);
    // Clear dirty flags on this node and on every child for_each_child() lists, then let
    // clear_dirty_override() clear any part this element owns but does not list. Not virtual:
    // the walk always reaches the children, so none of them can be left marked (its next
    // mark_dirty() would stop at a parent still marked, and it would freeze). The one exception
    // is a Collapsed element: layout skipped its subtree, which keeps its flags until it is laid
    // out again. The element itself is cleared, so set_visibility() showing it marks it afresh
    // and the mark reaches the window.
    void clear_dirty(DirtyFlags flags = DirtyFlags::All);

    // ---- thread affinity (ThreadCheck.hpp) ----
    // Declared with or without LIBWINUI_THREAD_CHECKS, so the layout never depends on it.
    // Reports `where` when this element's root belongs to a Window of another thread.
    void            check_tree_thread(const std::source_location& where) const;
    std::thread::id tree_thread_{}; // set on a Window's content, modal and popup roots; unset elsewhere

    std::unique_ptr<ElementMotion, ElementMotionDeleter> motion_;
    Visibility                                           visibility_               = Visibility::Visible;
    ElementTheme                                         requested_theme_          = ElementTheme::Default;
    bool                                                 hit_test_visible_         = true;
    bool                                                 allow_drop_               = false;
    bool                                                 has_children_transitions_ = false;
    bool                                                 laid_out_ = false; // arranged (Visible) at least once
    bool layers_composed_  = false; // set by Window (PaintTracker.hpp's layer probes)
    bool routed_           = false; // in a pointer route; ~Element() takes it out
    bool tracks_screen_    = false; // set_screen_tracking()
    bool on_screen_        = true;  // set by Window (PaintTracker.hpp) while tracked
    bool composes_visuals_ = false; // TreeAccess::set_composes_visuals()
    bool visuals_covered_  = false; // set by Window (PaintTracker.hpp's visual probes)
    bool visuals_listed_   = false; // in Window's list of elements showing visuals

    VisualId   visual_id_ = allocate_visual_id();
    DirtyFlags dirty_     = DirtyFlags::All; // start dirty so the first layout pass runs

    // ---- focus-visible state (see focus_visible() above) ----
    bool focus_visible_ = true;
    bool focused_       = false; // is_focused()

    // ---- enabled state (see is_enabled() above) ----
    bool enabled_      = true;
    bool enabled_gate_ = true;

    // ---- the window a root belongs to (window()); set on roots only ----
    Window* window_ = nullptr;

    // ---- keyboard focus bookkeeping (see has_focus_within() above) ----
    bool          keyboard_focused_ = false; // Window's focused element
    std::uint32_t focus_within_     = 0;     // this element plus descendants holding keyboard focus

    // ---- focus-scope opt-in (see focus_scope() above) ----
    bool focus_scope_ = false;


    // An explicit size narrows the range to itself, clipped to the
    // max, and the min wins over both.
    struct Range
    {
        float min;
        float max;
    };
    [[nodiscard]] static Range size_range(std::optional<float> size, float min_size, float max_size)
    {
        const float max = (std::max)((std::min)(size.value_or(std::numeric_limits<float>::infinity()), max_size), min_size);
        const float min = (std::max)((std::min)(size.value_or(0.0f), max), min_size);
        return {min, max};
    }
    [[nodiscard]] Range width_range() const
    {
        return size_range(width_, min_width_, max_width_);
    }
    [[nodiscard]] Range height_range() const
    {
        return size_range(height_, min_height_, max_height_);
    }
    // One axis of the offer: pulled into the range. An unbounded offer with a finite max is
    // offered the max, which the clamp does on its own.
    [[nodiscard]] static float constrain_offer(float offered, const Range& range)
    {
        return (std::max)(range.min, (std::min)(offered, range.max));
    }

    // ---- sizing state ----
    Thickness            margin_{};
    std::optional<float> width_{};
    std::optional<float> height_{};
    float                min_width_  = 0.0f;
    float                min_height_ = 0.0f;
    float                max_width_  = std::numeric_limits<float>::infinity();
    float                max_height_ = std::numeric_limits<float>::infinity();
    Alignment            h_align_    = Alignment::Stretch;
    Alignment            v_align_    = Alignment::Stretch;
    mutable Size         desired_{}; // measure_override()'s result (post min/max, pre-margin), consumed by arrange()

    // ---- the measure and arrange caches (see measure() and arrange()) ----
    mutable Size                measured_available_{-1.0f, -1.0f};
    mutable Size                measured_result_{};
    mutable const TextMeasurer* measured_with_             = nullptr;
    mutable const Theme*        measured_theme_            = nullptr;
    mutable std::uint64_t       measured_theme_generation_ = 0;
    mutable std::uint32_t       measured_pass_             = 0;
    std::uint32_t               arranged_pass_             = 0;
    Rect                        arranged_rect_{};
    mutable bool                measure_valid_  = false;
    mutable bool                measured_twice_ = false;
    mutable bool                arrange_valid_  = false;

    // ---- what the last frame drew (PaintTracker, Window::take_damage()) ----
    friend class detail::PaintTracker;
    mutable std::uint64_t painted_hash_          = 0; // its own commands, where and under what transform
    mutable std::uint64_t painted_children_hash_ = 0; // which children it drew, in order
    mutable Rect          painted_own_{};             // its own commands' area, window-client DIPs
    mutable Rect          painted_area_{};            // the whole subtree's
    mutable std::uint32_t painted_frame_ = 0;         // the PaintTracker frame it was drawn in

protected:
    /// What the last measure asked for, without the margin: how large the content is, where
    /// bounds() may be the larger slot a stretching parent gave it.
    [[nodiscard]] Size desired_content_size() const
    {
        return desired_;
    }

private:
    Rect bounds_{}; // what bounds() returns; arrange() writes it before arrange_override() runs

    // ---- reactivity / accessibility state ----
    std::vector<Subscription> subscriptions_;

    // What most elements never set, in one block made on the first set: a plain TextControl pays
    // one pointer for all of it.
    struct Extras
    {
        std::vector<Shortcut>                    shortcuts;         // add_shortcut()
        Callback<Handling(std::optional<Point>)> context_requested; // on_context_requested()
        Callback<Handling(const KeyEvent&)>      key_down;          // on_key_down()
        Callback<Handling(const PointerEvent&)>  wheel;             // on_wheel()
        Accessibility                            accessibility{};
        std::string                              tool_tip;                // set_tool_tip()
        std::optional<CursorShape>               cursor{};                // set_cursor()
        std::optional<bool>                      title_bar_drag_region{}; // see title_bar_drag_region()
        // The popup open_popup() opened, and how many times it was opened or updated.
        std::unique_ptr<PopupRequest> popup;
        std::uint64_t                 popup_generation = 0;
        // The pointer's route through this element while it is a dispatch root (handle_pointer()).
        std::unique_ptr<detail::PointerRoute, detail::PointerRouteDeleter> pointer_route;
        // The theme requested_theme() derived, and what it was derived from (Theme::generation, mode).
        std::unique_ptr<Theme> scoped_theme;
        std::uint64_t          scoped_generation = 0;
        bool                   scoped_dark       = false;
    };
    mutable std::unique_ptr<Extras> extras_;
    Extras&                         extras() const
    {
        if (!extras_)
        {
            extras_ = std::make_unique<Extras>();
        }
        return *extras_;
    }
    static const Accessibility& no_accessibility();
};

namespace detail
{
// Destroys `element`: at once, or, while a Window is handing input or a tick to its tree, when
// that call has returned. A container lets go of a child through this, because the child may be
// the one whose handler asked for it (a page's own "Next" button, a row's own "Remove"), and
// frames of it are then still on the stack. The element is detached from its parent here, and a
// parked one stops following its bindings at once, so it never sees state it wasn't built for.
void retire(std::unique_ptr<Element> element);
// Takes `element` out of its window alive, for a container's orphan(): focus inside it is cleared,
// the pointer routes forget it and the window's tab ring is rebuilt. It is detached from its parent
// here, and handed back.
[[nodiscard]] std::unique_ptr<Element> orphan(std::unique_ptr<Element> element);

// The tree linkage for library code that is not an Element: retire(), the window's frame.
struct TreeAccess
{
    [[nodiscard]] static AutomationProviders automation_providers(Element& element)
    {
        AutomationProviders out;
        element.automation_providers(out);
        return out;
    }
    static void collect_live(Element& element, std::vector<Element*>& out)
    {
        element.collect_live(out);
    }
    [[nodiscard]] static PopupRequest* popup_request(const Element& element) noexcept
    {
        return element.popup_request();
    }
    static void dismiss_popup(Element& element, PopupCloseReason reason)
    {
        element.dismiss_popup(reason);
    }
    static void set_parent(Element& element, Element* parent)
    {
        element.set_parent(parent);
    }
    static void unlink_subscriptions(Element& element) noexcept
    {
        element.unlink_subscriptions();
    }
    static void clear_dirty(Element& element, DirtyFlags flags = DirtyFlags::All)
    {
        element.clear_dirty(flags);
    }
    static void sync_composition(Element& element, Compositor& compositor, const Theme& theme)
    {
        element.sync_composition(compositor, theme);
    }
    [[nodiscard]] static bool layers_composed(const Element& element)
    {
        return element.layers_composed();
    }
    // Marks an element whose sync_composition_override() keeps compositor visuals over its own
    // drawing, so the Window checks each frame whether anything covers them.
    static void set_composes_visuals(Element& element)
    {
        element.composes_visuals_ = true;
    }
    // Whether something hides where the element's compositor visuals would show: drawing later in
    // the frame (a popup, a dialog), a clip that cuts them, a transform or an opacity. The visuals
    // sit above the whole frame, so the element removes them and draws what they showed itself.
    // Also true while the element is not drawn at all (collapsed, or left out by its parent).
    [[nodiscard]] static bool visuals_covered(const Element& element)
    {
        return element.visuals_covered_;
    }
};

// Window::layout()'s incremental path, reading and driving the measure and arrange caches.
struct LayoutAccess
{
    // Place an element that is never measured or arranged (an automation-only node standing for
    // part of its owner) at `rect`.
    static void set_bounds(Element& e, const Rect& rect)
    {
        e.bounds_ = rect;
    }
    [[nodiscard]] static bool own_layout_invalid(const Element& e)
    {
        return !e.measure_valid_ || !e.arrange_valid_;
    }
    [[nodiscard]] static bool measure_invalid(const Element& e)
    {
        return !e.measure_valid_;
    }
    // The theme `e`'s parent hands it, worked out from the window's `root` theme down through the
    // ancestors' requested themes.
    [[nodiscard]] static const Theme& handed_theme(const Element& e, const Theme& root)
    {
        if (e.parent_ == nullptr)
        {
            return root;
        }
        const Element& parent = *e.parent_;
        const Theme&   above  = handed_theme(parent, root);
        return parent.requested_theme_ == ElementTheme::Default ? above : parent.scoped_theme(above);
    }
    // Whether `e` was last measured with `measurer` and `handed`, so it can be measured again in
    // its old space. A container that hands its children a theme of its own fails this.
    [[nodiscard]] static bool measured_with(const Element& e, const TextMeasurer& measurer, const Theme& handed)
    {
        return e.measured_with_ == &measurer && e.measured_theme_ == &handed &&
               e.measured_theme_generation_ == handed.generation();
    }
    // Measures `e` again with what it was last offered; true when its size came out the same and
    // no other offer in its last pass hangs on it.
    static bool remeasure(Element& e, const TextMeasurer& measurer, const Theme& handed)
    {
        const Size before = e.measured_result_;
        const bool twice  = e.measured_twice_;
        const Size after  = e.measure(e.measured_available_, measurer, handed);
        return after == before && !twice;
    }
    static void invalidate_own_measure(Element& e)
    {
        e.measure_valid_ = false;
        e.arrange_valid_ = false;
    }
    // Arranges `e` again in the rect it was last given.
    static void rearrange(Element& e)
    {
        e.arrange_valid_ = false;
        e.arrange(e.arranged_rect_);
    }
};

// How many Elements this thread has destroyed or orphaned so far. A holder of raw pointers into a
// tree (Window's tab ring) compares it with the value it last saw to know whether to look again.
[[nodiscard]] std::uint64_t removed_element_count() noexcept;

// While one exists on this thread, retire() parks what it is given; the outermost one destroys
// the lot as it goes. Window holds one around every dispatch into its tree.
class RetireScope
{
public:
    RetireScope() noexcept;
    ~RetireScope();
    RetireScope(const RetireScope&)            = delete;
    RetireScope& operator=(const RetireScope&) = delete;
};

// The children a container has let go of that are still playing their exit transition
// (src/ui/ExitGhosts.hpp). Opaque here; a container holds it through ExitGhostsPtr, created on
// the first exit.
class ExitGhosts;
struct ExitGhostsDelete
{
    void operator()(ExitGhosts* ghosts) const noexcept;
};
using ExitGhostsPtr = std::unique_ptr<ExitGhosts, ExitGhostsDelete>;
} // namespace detail

} // namespace ui

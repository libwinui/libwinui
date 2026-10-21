// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Paint.hpp"
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

/// How a Swipe side shows its items. Reveal opens a drawer of items at their natural widths, as
/// far as it is dragged. Execute shows one item as wide as the control, invoked by swiping it
/// all the way, as in "swipe to archive".
enum class SwipeMode
{
    Reveal,  ///< A drawer of items at their natural widths. A tap invokes one.
    Execute, ///< One item as wide as the control, invoked by swiping past the threshold.
};

/// What the strip does after an item is invoked. Auto closes, as Close does, in both modes.
enum class SwipeBehaviorOnInvoked
{
    Auto,       ///< Close after the item runs.
    Close,      ///< Close after the item runs.
    RemainOpen, ///< Leave the strip open after the item runs.
};

class SwipeControl;
struct SwipeItem;

/// The arguments of an item's on_invoked: the item that was invoked, whose text or icon the
/// handler may change, and the control it was swiped from.
struct SwipeItemInvokedEventArgs
{
    SwipeControl& swipe_control; ///< The control the item was swiped from.
    SwipeItem&    item;          ///< The invoked item. Its text and icon may be changed here.
};

/// SwipeItem::on_invoked's handler: a callable that takes the event args, or one that takes
/// nothing.
///
/// Copyable, since SwipeItem lives in a std::vector that a caller builds with a brace list.
///
/// Example:
/// @code
/// bool flagged = false;
/// ui::SwipeItem flag{ .text = "Flag", .on_invoked = [&] { flagged = true; } };
/// ui::SwipeItem mark_read{ .text       = "Mark read",
///                          .on_invoked = [](ui::SwipeItemInvokedEventArgs& e) { e.item.text = "Marked"; } };
/// @endcode
class SwipeItemInvokedHandler
{
public:
    /// No handler. Invoking it does nothing.
    SwipeItemInvokedHandler() = default;
    /// Wraps a callable taking `SwipeItemInvokedEventArgs&`, or one taking nothing.
    template <typename F>
        requires std::invocable<F&> || std::invocable<F&, SwipeItemInvokedEventArgs&>
    SwipeItemInvokedHandler(F handler) // NOLINT(google-explicit-constructor): a lambda is a handler
    {
        if constexpr (std::invocable<F&, SwipeItemInvokedEventArgs&>)
        {
            fn_ = std::move(handler);
        }
        else
        {
            fn_ = [handler = std::move(handler)](SwipeItemInvokedEventArgs&) mutable { handler(); };
        }
    }

    /// Calls the handler, if there is one.
    void operator()(SwipeItemInvokedEventArgs& args) const
    {
        if (fn_)
        {
            fn_(args);
        }
    }
    /// Whether a handler is set.
    [[nodiscard]] explicit operator bool() const
    {
        return static_cast<bool>(fn_);
    }

private:
    std::function<void(SwipeItemInvokedEventArgs&)> fn_;
};

/// One action in a SwipeControl's strip: a label under an icon, its own colors, and what
/// happens when it is invoked. A value type, built with designated initializers and handed over
/// in a SwipeItems list.
///
/// Example:
/// @code
/// void archive_message(); // the app's own
/// ui::SwipeItem archive{
///     .text       = "Archive",
///     .icon       = ui::IconGlyph{0xE7B8},
///     .background = ui::Color::from_argb(0xFF3E6FA7),
///     .foreground = ui::colors::White,
///     .on_invoked = [&] { archive_message(); },
/// };
/// @endcode
struct SwipeItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The text. Falls back to the command's text.
    std::string text{};

    /// The icon, drawn 16 DIPs tall above the text. Falls back to the command's icon.
    IconSource icon{};

    /// A color, a brush or a ThemeColor. In Execute mode a gradient spans the whole control.
    /// Empty uses the theme's item background in Reveal, and in Execute the theme's two colors
    /// for before and after the swipe passes its threshold.
    Paint background{};

    /// The text and icon color. Unset uses the theme's item foreground, which in Execute is a pair
    /// for before and after the threshold.
    std::optional<Color> foreground{};

    /// What the strip does after the item is invoked.
    SwipeBehaviorOnInvoked behavior_on_invoked = SwipeBehaviorOnInvoked::Auto;

    /// A command run after `on_invoked`, while it is enabled. Its text and icon fill the text and
    /// icon left empty.
    CommandRef command{};
    /// Handed to the command's execute.
    std::string command_parameter{};

    /// Fires on a tap in Reveal mode or a completed swipe in Execute mode, before the command
    /// runs.
    SwipeItemInvokedHandler on_invoked{};

    /// The text drawn: `text`, or the command's text when `text` is empty.
    [[nodiscard]] std::string display_text() const;
    /// The icon drawn: `icon`, or the command's icon when `icon` is empty.
    [[nodiscard]] const IconSource& display_icon() const;
};

/// One side's items and how they are revealed. An Execute set shows a single item, as wide as
/// the control.
struct SwipeItems
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The strip's items, in order from the left.
    std::vector<SwipeItem> items;
    /// Reveal (a drawer) or Execute (swipe to run).
    SwipeMode mode = SwipeMode::Reveal;
};

/// Reveals a strip of action items beside its content when the content is dragged sideways:
/// the "swipe a list row to reveal Delete/Archive" pattern.
///
/// Dragging right reveals the left items, and dragging left the right ones. The control owns its
/// content, which stays fully interactive. The items work with the mouse only and are not
/// exposed to accessibility tools. The declarative form is ui::Swipe.
///
/// Example:
/// @code
/// void reply();      // the app's own
/// void delete_row(); // the app's own
/// ui::SwipeItem reply_item{ .text = "Reply", .icon = ui::IconGlyph{0xE8C2}, .on_invoked = [&] { reply(); } };
/// ui::SwipeItem delete_item{ .text       = "Delete",
///                            .icon       = ui::IconGlyph{0xE74D},
///                            .background = ui::theme_colors::Critical,
///                            .on_invoked = [&] { delete_row(); } };
///
/// window.set_content(ui::Swipe{
///     .left_items  = { reply_item },
///     .right_items = { delete_item },
///     .right_mode  = ui::SwipeMode::Execute,
///     .height      = 68,
///     .content     = ui::Text{ .text               = "Swipe me",
///                              .margin             = ui::Thickness{12},
///                              .vertical_alignment = ui::Alignment::Center },
/// });
///
/// // Imperatively:
/// auto swipe = std::make_unique<ui::SwipeControl>();
/// swipe->set_content(std::make_unique<ui::TextControl>("Swipe me"));
/// swipe->set_left_items({ .items = { reply_item } });
/// swipe->set_right_items({ .items = { delete_item }, .mode = ui::SwipeMode::Execute });
/// @endcode
class SwipeControl final : public Element
{
public:
    /// No content and no items; closed.
    SwipeControl() = default;
    ~SwipeControl() override;
    SwipeControl(SwipeControl&&) = default;

    // ---- content ----
    /// Takes ownership of the swiped content and returns a non-owning pointer to it. The content
    /// is always measured, arranged, drawn and reachable. Only its drawn position changes.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "SwipeControl content must be an Element");
        T* observer = content.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The swiped content (owned by the control), or null.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    // ---- items ----
    /// Revealed by dragging the content to the right.
    [[nodiscard]] const SwipeItems& left_items() const
    {
        return left_items_;
    }
    /// Replaces the item list and snaps closed.
    void set_left_items(SwipeItems items);
    /// Revealed by dragging the content to the left.
    [[nodiscard]] const SwipeItems& right_items() const
    {
        return right_items_;
    }
    /// Replaces the item list and snaps closed.
    void set_right_items(SwipeItems items);

    // ---- the content presenter's chrome ----
    /// The fill behind the content, moving with it. Transparent by default.
    void set_background(std::optional<Color> color)
    {
        background_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// The fill behind the content as a theme resource, re-read on a theme change.
    void set_background(ThemeColor resource)
    {
        background_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border color around the content, moving with it.
    void set_border_brush(std::optional<Color> color)
    {
        border_brush_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border color as a theme resource, re-read on a theme change.
    void set_border_brush(ThemeColor resource)
    {
        border_brush_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border width in DIPs, inside the control's bounds. Lays the control out again.
    void set_border_thickness(Thickness thickness)
    {
        border_thickness_ = thickness;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- open/closed state ----
    /// Positive: the left items revealed by this many DIPs. Negative: the right items revealed by
    /// |offset|. Zero: closed.
    [[nodiscard]] float offset() const
    {
        return offset_;
    }
    /// Whether a strip shows, or is sliding open.
    [[nodiscard]] bool is_open() const
    {
        return offset_ != 0.0f || (animating_ && target_ != 0.0f);
    }
    /// Slides the strip shut.
    void close();

    /// Visits the content, the only child. The control itself is not focusable, and whatever is
    /// focusable in the content is always in the tab order. The items are not children: they
    /// work with the mouse only and are not exposed to accessibility tools.
    void for_each_child(ChildVisitor visit) const override
    {
        if (content_)
        {
            visit(*content_);
        }
    }

protected:
    /// The content's size plus the border, and at least an item's minimum height while there are
    /// items to reveal. Also measures the items' widths.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Arranges the content inside the border. offset() shifts it only when drawing.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the revealed strip, then the background, border and content shifted by offset().
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// A horizontal drag moves the strip and takes the gesture from the content. A tap on an item
    /// invokes it, and a tap on the content while open closes the strip.
    bool pointer_override(const PointerEvent& e) override;
    /// A press and release the content took still start and end a drag, so a swipe can begin on
    /// a button.
    void handled_pointer_override(const PointerEvent& e) override;
    /// Open or dragged, the strip covers the content.
    [[nodiscard]] bool covers_children_at(Point p) const override;
    /// Ticks the content, then advances the 250 ms snap, invoking an Execute item as it settles.
    void tick_override(Duration delta) override;


private:
    enum class Side
    {
        None,
        Left,
        Right,
    };

    void                      recompute_item_widths(const TextMeasurer& measurer, const Theme& theme) const;
    [[nodiscard]] Side        side_at(Point pos) const;
    [[nodiscard]] std::size_t item_index_at(Side side, Point pos) const;
    [[nodiscard]] float       extent(Side side) const; // how far that side opens
    [[nodiscard]] float       threshold(Side side) const;
    void                      invoke(Side side, std::size_t index);
    void                      snap_to_rest();
    void                      animate_to(float target);
    void                      watch_commands();
    void render_side(Renderer& renderer, const Theme& theme, Side side, const Rect& revealed) const;
    void draw_item_content(Renderer&        renderer,
                           const Theme&     theme,
                           const SwipeItem& item,
                           const Rect&      box,
                           const Color&     foreground) const;

    std::unique_ptr<Element>  content_;
    SwipeItems                left_items_;
    SwipeItems                right_items_;
    std::vector<Subscription> command_changed_; // every item's command, both sides
    BackgroundBrush           background_;
    BackgroundBrush           border_brush_;
    Thickness                 border_thickness_{};

    float offset_ = 0.0f;
    Rect  content_base_rect_{}; // content_'s own arrange_override()-computed rect, before offset_ (mirrors Parallax)

    // The snap: an eased slide from `from_` to `target_`, and the item to invoke once an Execute
    // swipe has slid fully open.
    bool        animating_    = false;
    float       from_         = 0.0f;
    float       target_       = 0.0f;
    double      anim_ms_      = 0.0;
    Side        invoke_side_  = Side::None;
    std::size_t invoke_index_ = detail::NoIndex;

    // Filled by measure_override(), read by render() and the pointer handling.
    mutable std::vector<float> left_item_widths_;
    mutable std::vector<float> right_item_widths_;
    mutable float              left_extent_  = 0.0f;
    mutable float              right_extent_ = 0.0f;

    // ---- drag state ----
    bool pointer_down_ = false;
    bool drag_owns_gesture_ = false; // true once horizontal movement has committed to a drag (steals the gesture from content_)
    Point       drag_start_pos_{};
    float       drag_start_offset_ = 0.0f;
    Side        pressed_side_      = Side::None; // which side's item (if any) the current press landed on
    std::size_t pressed_index_     = detail::NoIndex;

protected:
    void enabled_changed() override;
};

/// Reveals a strip of action items when its child is dragged sideways, as a list row's swipe
/// actions. Builds a ui::SwipeControl. The callbacks are on each ui::SwipeItem, so the spec has
/// no `.on_*` handler of its own for them.
///
/// Example:
/// @code
/// struct Row { std::string name; };
///
/// ui::Spec swipe_row(const Row& row, std::function<void()> remove) // captures by value
/// {
///     return ui::Swipe{
///         .left_items  = {{.text = "Flag", .icon = ui::Symbol::Flag}},
///         .right_items = {{.text       = "Delete",
///                          .background = ui::Color::from_rgba(196, 43, 28),
///                          .on_invoked = [remove] { remove(); }}},
///         .right_mode  = ui::SwipeMode::Execute,
///         .content     = ui::Text{.text = row.name, .margin = ui::Thickness{12.0f}},
///     };
/// }
/// @endcode
struct Swipe
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Swipe>` points at.
    using control_type = SwipeControl;

    // ---- properties ----
    /// Revealed by dragging the content to the right.
    Binding<std::vector<SwipeItem>> left_items{};
    /// Reveal, the default, shows the items to click. Execute invokes the single item on release.
    SwipeMode left_mode = SwipeMode::Reveal;
    /// Revealed by dragging the content to the left.
    Binding<std::vector<SwipeItem>> right_items{};
    /// As `left_mode`, for `right_items`.
    SwipeMode right_mode = SwipeMode::Reveal;
    /// The fill, border color and border width behind the content, which move with it. A color
    /// is fixed or a theme resource, read again when the theme changes.
    Binding<Paint>     background{};
    Binding<Paint>     border_brush{};     ///< See `background`.
    Binding<Thickness> border_thickness{}; ///< See `background`.

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False turns swiping off.
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
    ControlOut<Swipe> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The row content that slides to reveal the items.
    Spec content{};

    /// Consumes the spec and returns the built ui::SwipeControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

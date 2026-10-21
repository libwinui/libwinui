// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// One section: a header and the content shown while it is selected.
/// Passed to `SectionsControl::add_item()`, which takes ownership of the content.
struct SectionsControlItem
{
    std::string              header{};  ///< The header text in the header row.
    std::unique_ptr<Element> content{}; ///< Shown below the headers while the item is selected.
};

/// A row of large text headers over the selected item's content.
///
/// An optional title sits above the header row, and optional left/right header elements flank
/// it. When the headers do not all fit, they form a carousel by default. The selected header
/// leads the row and the rest follow it, wrapping around. Only then do the keyboard and the
/// navigation buttons shown on hover wrap too. The selected header is underlined with an accent
/// bar. A change of selection flies the old content out and the new one in from the side the
/// selection moved to. The header row is the tab stop. Left/Right select the previous/next item
/// and Home/End the first/last. Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageDown and Ctrl+PageUp step
/// the selection from anywhere inside. `ui::Sections` is the declarative form.
///
/// Example:
/// @code
/// window.set_content(ui::Sections{
///     .title      = std::string{"EMAIL"},
///     .min_height = 400.0f,
///     .on_selection_changed = [](std::size_t index) { /* e.g. mark items read */ },
///     .items = {
///         ui::Section{.header = "All",     .content = ui::Text{.text = "all emails go here."}},
///         ui::Section{.header = "Unread",  .content = ui::Text{.text = "unread emails go here."}},
///         ui::Section{.header = "Flagged", .content = ui::Text{.text = "flagged emails go here."}},
///     },
/// });
///
/// // Imperatively:
/// auto sections = std::make_unique<ui::SectionsControl>();
/// sections->set_title("EMAIL");
/// ui::TextControl* all = sections->add_item("All", std::make_unique<ui::TextControl>("all emails"));
/// sections->add_item("Unread", std::make_unique<ui::TextControl>("unread emails"));
/// sections->on_selection_changed([](std::size_t index) { /* ... */ });
/// sections->set_selected_index(1);
/// @endcode
class SectionsControl final : public Element, private ValueProvider
{
public:
    /// An empty Sections: no title, no items, carousel enabled.
    SectionsControl() = default;

    // ---- items ----

    /// Appends an item, taking ownership of its content, and returns a non-owning observer to the
    /// content. The first item added becomes selected.
    template <typename T>
    T* add_item(std::string header, std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "Sections content must be an Element");
        T* observer = content.get();
        add_item(SectionsControlItem{std::move(header), std::move(content)});
        return observer;
    }
    /// Appends `item`, taking ownership of its content (which may be null for a header-only item).
    /// The first item added becomes selected.
    void add_item(SectionsControlItem item);

    /// The number of items added.
    [[nodiscard]] std::size_t item_count() const
    {
        return items_.size();
    }
    /// The header text of item `index`; an empty string when out of range.
    [[nodiscard]] const std::string& header(std::size_t index) const;

    // ---- properties ----

    /// The title above the header row; empty collapses the title row.
    [[nodiscard]] const std::string& title() const
    {
        return title_;
    }
    /// Sets the title, updates the layout as the title row appears or collapses, and redraws.
    void set_title(std::string title)
    {
        title_ = std::move(title);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The selected item; unset while there are no items.
    [[nodiscard]] std::optional<std::size_t> selected_index() const
    {
        return detail::optional_index(selected_index_);
    }
    /// Selects item `index`, clamped to the items. Does nothing, and fires nothing, if the item
    /// is already selected.
    void set_selected_index(std::size_t index);
    /// Fired with the new index whenever the selection changes, by click, keyboard, the
    /// navigation buttons or `set_selected_index()`. Replaces any previous handler.
    void on_selection_changed(Callback<void(std::size_t)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    /// Whether the headers form a carousel, true by default. Headers that do not fit then rotate
    /// so the selected one leads. False keeps them in order and scrolls the row to the selection.
    [[nodiscard]] bool header_carousel_enabled() const
    {
        return carousel_enabled_;
    }
    /// Updates the layout and redraws.
    void set_header_carousel_enabled(bool enabled)
    {
        carousel_enabled_ = enabled;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// An element left of the headers. Takes ownership (replacing any previous one) and returns a
    /// non-owning observer.
    template <typename T>
    T* set_left_header_content(std::unique_ptr<T> element)
    {
        T* observer = element.get();
        adopt(left_header_, std::move(element));
        return observer;
    }
    /// Like `set_left_header_content()`, for the right end of the header row.
    template <typename T>
    T* set_right_header_content(std::unique_ptr<T> element)
    {
        T* observer = element.get();
        adopt(right_header_, std::move(element));
        return observer;
    }


    // ---- geometry, from the last arrange ----

    /// Whether the headers all fit, so they stay in place instead of rotating.
    [[nodiscard]] bool static_headers() const
    {
        return static_headers_;
    }
    /// The header of item `index` as drawn, or nullopt if it is clipped out of the row.
    [[nodiscard]] std::optional<Rect> header_bounds(std::size_t index) const;
    /// The header row's rect.
    [[nodiscard]] Rect header_row() const
    {
        return header_row_;
    }
    /// The area the selected item's content is arranged in, below the header row.
    [[nodiscard]] Rect content_bounds() const
    {
        return content_rect_;
    }
    /// The navigation buttons, while they show.
    [[nodiscard]] std::optional<Rect> previous_button_bounds() const;
    /// Like `previous_button_bounds()`, for the next button.
    [[nodiscard]] std::optional<Rect> next_button_bounds() const;
    /// Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+PageDown / Ctrl+PageUp, from anywhere inside the Sections.
    bool handle_shortcut(const KeyEvent& e) override;
    /// Whether the header row has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The header row is a tab stop while enabled and non-empty.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && !items_.empty();
    }
    /// Only a press on the header row focuses the Sections. The selected item's content takes
    /// focus on its own.
    [[nodiscard]] bool takes_focus_at(Point p) const override
    {
        return header_row_.contains(p);
    }
    /// Tab order: the left header, the header row, the right header, then the selected content.
    void collect_focusables(std::vector<Element*>& out) override;
    /// Lists the left header, the right header and the selected item's content. An unselected
    /// item's content is set aside, and nothing reaches it until it is selected again.
    void for_each_child(ChildVisitor visit) const override;
    /// Exposed to accessibility tools as a tab control whose value is the selected header.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Tab;
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    /// A click on a header selects it, and a click on a navigation button moves by one. Other
    /// input goes to the selected content.
    bool pointer_override(const PointerEvent& e) override;
    void hit_test_children(Point p, std::vector<Element*>& out) const override;
    /// Also clears the content still flying out after a selection change.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Advances the selection-change fly-in/fade animation.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    template <typename T>
    void adopt(std::unique_ptr<Element>& slot, std::unique_ptr<T> element)
    {
        if (element)
        {
            set_parent_of(*element, this);
        }
        slot = std::move(element);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    void                   select(std::size_t index, int direction); // direction: +1 forward, -1 back, 0 from the index
    void                   move(int delta);                          // previous/next, wrapping while the headers rotate
    [[nodiscard]] Element* content(std::size_t index) const
    {
        return index < items_.size() ? items_[index].content.get() : nullptr;
    }
    [[nodiscard]] std::size_t      header_at(Point p) const;
    [[nodiscard]] static TextStyle header_style(const Theme& theme);

    struct Slot
    {
        std::size_t item;
        Rect        rect;
    };

    std::vector<SectionsControlItem> items_;
    std::string                      title_;
    std::size_t                      selected_index_   = detail::NoIndex;
    bool                             carousel_enabled_ = true;
    std::unique_ptr<Element>         left_header_;
    std::unique_ptr<Element>         right_header_;

    // ---- layout ----
    mutable std::vector<float> header_widths_;
    mutable float              title_height_ = 0.0f;
    mutable Size               left_desired_{};
    mutable Size               right_desired_{};
    Rect                       title_rect_{};
    Rect                       header_row_{};
    Rect                       content_rect_{};
    std::vector<Slot>          slots_;
    bool                       static_headers_ = true;
    float                      static_scroll_  = 0.0f; // carousel off: the row scrolled to the selection

    // ---- interaction ----
    std::size_t hovered_header_ = detail::NoIndex;
    std::size_t pressed_header_ = detail::NoIndex;
    bool        header_hovered_ = false; // shows the navigation buttons
    int         nav_hovered_    = 0;     // -1 previous, +1 next
    int         nav_pressed_    = 0;

    // ---- selection-change animation ----
    std::size_t leaving_index_ = detail::NoIndex; // the item flying out
    int         direction_     = 0;               // +1: new content comes from the right
    double      clock_ms_      = -1.0;            // < 0: settled
    bool        ticking_       = false;

    Callback<void(std::size_t)> on_selection_changed_;

protected:
    void enabled_changed() override;
    /// Left/Right select the previous/next item, Home/End the first/last.
    bool key_override(const KeyEvent& e) override;
    void focus_changed(bool focused) override;

protected:
    /// Around the header row, square.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return header_row_;
    }
    [[nodiscard]] float focus_ring_corner_radius(const Theme& /*theme*/) const override
    {
        return 0.0f;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value = this;
    }
    [[nodiscard]] std::string text_value() const override
    {
        return header(selected_index_);
    }
    [[nodiscard]] bool text_value_read_only() const override
    {
        return true;
    }
    bool set_text_value(std::string_view /*text*/) override
    {
        return false;
    }
};

/// One section: a header and the content shown while it is selected. Written inside
/// the `.items` of a ui::Sections.
struct Section
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The item's header text in the header row.
    std::string header{};
    /// What the Sections shows while this item is selected.
    Spec content{};
};

/// A built, owned list of Sections items. Callers never name it, since a braced list of
/// `ui::Section{...}` converts to it.
class SectionList
{
public:
    /// One item with its content already built.
    struct Entry
    {
        std::string              header;
        std::unique_ptr<Element> node{};
    };

    /// No items.
    SectionList() = default;
    /// Builds each item's content in order. Implicit, so `.items = {ui::Section{...}, ...}` works.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, Section> && ...))
    SectionList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        entries.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    /// Move-only: the entries own their content.
    SectionList(const SectionList&)            = delete;
    SectionList& operator=(const SectionList&) = delete;
    SectionList(SectionList&&)                 = default;
    SectionList& operator=(SectionList&&)      = default;

    /// The items, in header order.
    std::vector<Entry> entries;

private:
    void add(Section&& item)
    {
        entries.push_back(Entry{std::move(item.header), item.content.take()});
    }
};

/// Large text headers over swappable content. Builds a ui::SectionsControl.
///
/// Example:
/// @code
/// ui::Property<std::size_t> current_folder{0};
/// window.set_content(ui::Sections{
///     .title                = "EMAIL",
///     .on_selection_changed = [&](std::size_t i) { current_folder = i; },
///     .items                = {
///         ui::Section{ .header = "All", .content = ui::Text{ .text = "all emails go here." } },
///         ui::Section{ .header = "Unread", .content = ui::Text{ .text = "unread emails go here." } },
///         ui::Section{ .header = "Flagged", .content = ui::Text{ .text = "flagged emails go here." } },
///     },
/// });
/// @endcode
struct Sections
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Sections>` points at.
    using control_type = SectionsControl;

    // ---- properties ----
    /// The small caption above the headers; empty draws none.
    Binding<std::string> title{};
    /// The selected item; clamped to the items.
    TwoWayField<std::size_t> selected_index{0};
    /// Headers that do not fit rotate so the selected one leads. False scrolls the row instead.
    Binding<bool> header_carousel_enabled{true};

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
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Called with the new index whenever the selection moves. A bound value doesn't run it.
    Callback<void(std::size_t)> on_selection_changed{};
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
    ControlOut<Sections> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The items, in header order.
    SectionList items{};
    /// An element left of the headers.
    Spec left_header_content{};
    /// An element right of the headers.
    Spec right_header_content{};

    /// Consumes the spec and returns the built ui::SectionsControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

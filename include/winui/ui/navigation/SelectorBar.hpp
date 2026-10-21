// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// One segment of a SelectorBar: its text and an optional icon before it.
///
/// Converts from a string, so a list of plain strings needs no item type spelled out:
/// `.items = {"Recent", "Shared"}`, or with icons
/// `.items = {{"Recent", ui::Symbol::Clock}, {"Shared", ui::Symbol::Share}}`.
struct SelectorBarItem
{
    /// A segment showing `item_text`, with `icon` before it (empty: none).
    SelectorBarItem(std::string item_text, IconSource icon = {}) :
        text(std::move(item_text)),
        icon(std::move(icon)) // NOLINT(google-explicit-constructor)
    {
    }
    /// A segment showing `item_text`.
    SelectorBarItem(const char* item_text) : text(item_text) // NOLINT(google-explicit-constructor)
    {
    }

    std::string text; ///< The segment's text.
    IconSource  icon; ///< Drawn before the text at 0.8 of a 20-DIP symbol; empty draws none.

    /// Same text and icon.
    friend bool operator==(const SelectorBarItem&, const SelectorBarItem&) = default;
};

/// A row of text segments, each with an optional icon. Exactly one is selected, marked by an
/// accent pill under its text.
///
/// The items own no content, so the app switches what is shown below in on_selection_changed().
/// A press on an item selects it, and with focus, Left/Right move the selection. The whole strip
/// is one tab stop. The declarative form is ui::SelectorBar.
///
/// Example:
/// @code
/// auto  panel = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* bar   = panel->add(std::make_unique<ui::SelectorBarControl>(
///     std::vector<ui::SelectorBarItem>{"Recent", "Shared", "Favorites"}));
/// auto* shown = panel->add(std::make_unique<ui::TextControl>("Recent files"));
/// bar->on_selection_changed([bar, shown](std::size_t) { shown->set_text(bar->selected_text() + " files"); });
/// window.set_content(std::move(panel));
/// @endcode
class SelectorBarControl final : public Element, private ValueProvider
{
public:
    /// An empty strip: nothing drawn, not focusable.
    SelectorBarControl() = default;
    /// A strip of `items`, `selected_index` selected (clamped). Raises nothing.
    explicit SelectorBarControl(std::vector<SelectorBarItem> items, std::size_t selected_index = 0);

    // ---- items ----
    /// The items, in order.
    [[nodiscard]] const std::vector<SelectorBarItem>& items() const
    {
        return items_;
    }
    /// Replaces the item list and resets the selection to index 0 without raising
    /// on_selection_changed(). Updates the layout.
    void set_items(std::vector<SelectorBarItem> items);

    /// The selected item's index; 0 while the list is empty.
    [[nodiscard]] std::size_t selected_index() const
    {
        return selected_index_;
    }
    /// Selects the item at `index`, clamped to the item list. Does nothing, and fires nothing, if
    /// the item is already selected or the list is empty.
    void set_selected_index(std::size_t index);

    /// The currently selected item's text, or an empty string if there are no items.
    [[nodiscard]] const std::string& selected_text() const;


    /// Fired whenever the selection changes: an item click, the arrow keys or
    /// set_selected_index(). A call that lands on the already selected item does not fire it.
    void on_selection_changed(Callback<void(std::size_t)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- focus ----
    /// Whether the strip holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Not a tab stop with fewer than two items, since there is nothing to switch.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && items_.size() > 1;
    }

    // ---- automation ----
    /// AutomationRole::Tab. The items have no content of their own, so the strip exposes the
    /// selected item's text as a read-only value.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Tab;
    }

protected:
    // ---- layout ----
    /// A single row sized to fit each item's own text, without stretching to fill the strip, plus
    /// room for the pill at the bottom of every item.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- render ----
    /// Each item's icon and text, the selected one's pill, and the focus ring while focused.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- input ----
    /// Hover tracking, and a press on an item selects it.
    bool pointer_override(const PointerEvent& e) override;

    // ---- motion ----
    /// Advances the selection pill's grow-in.
    void tick_override(Duration delta) override;


private:
    // Left/Right move between segments.
    // Claimed so Window's directional focus navigation leaves them alone
    // (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    [[nodiscard]] std::size_t hit_test(Point p) const;
    void                      select(std::size_t index); // clamps, no-op on reselect, marks dirty, fires the callback


    std::vector<SelectorBarItem> items_;
    std::size_t                  selected_index_ = 0;
    std::size_t                  hovered_index_  = detail::NoIndex;
    std::size_t                  pressed_index_  = detail::NoIndex;

    // `ticking_` gates the grow-in on a host that drives tick_override(), so a headless test sees a
    // settled pill instead of one frozen at 4px.
    double pill_anim_ms_   = 0.0;
    bool   pill_animating_ = false;
    bool   ticking_        = false;

    mutable float              row_height_ = 0.0f;
    mutable std::vector<float> item_widths_;

    Callback<void(std::size_t)> on_selection_changed_;

protected:
    void enabled_changed() override;
    /// ArrowLeft/ArrowRight select the previous/next item while focused (no wrap-around).
    bool key_override(const KeyEvent& e) override;
    /// Called by Window as focus moves; redraws.
    void focus_changed(bool focused) override;

protected:
    /// A square ring, a little tighter around the strip than the usual focus ring.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
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
    // selected_text().
    [[nodiscard]] std::string text_value() const override
    {
        return selected_text();
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

/// A horizontal segmented selector. Builds a ui::SelectorBarControl.
///
/// Typically paired with a ui::Match that shows the page for the selected segment.
///
/// Example:
/// @code
/// ui::Property<std::size_t> tab{0};
///
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::SelectorBar{
///             .items          = {{"Recent", ui::Symbol::Clock}, "Shared", {"Favorites", ui::Symbol::Favorite}},
///             .selected_index = tab,
///         },
///         ui::Match<std::size_t>{
///             .value   = tab,
///             .content = [](const std::size_t& i) -> ui::Spec
///             { return ui::Text{.text = std::format("Page {}", i + 1)}; },
///         },
///     },
/// });
/// @endcode
struct SelectorBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<SelectorBar>` points at.
    using control_type = SelectorBarControl;

    // ---- properties ----
    /// The segments, left to right: text, each with an optional icon.
    Binding<std::vector<SelectorBarItem>> items{};
    /// The selected segment; clamped into the item range.
    TwoWayField<std::size_t> selected_index{0};

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
    /// Fires with the new index when a click or the arrow keys change the selection. A bound write
    /// doesn't run it.
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
    ControlOut<SelectorBar> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::SelectorBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

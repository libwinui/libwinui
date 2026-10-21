// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
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

class BreadcrumbFlyoutList; // Breadcrumb.cpp: the ellipsis flyout's rows

/// A navigation trail (Home > Folder > File) of plain text items, root first.
///
/// Every item but the last is a small inline button that raises on_item_click() with its
/// index. The last is the current location. It is not clickable, has no trailing chevron and is
/// never hovered or highlighted by the keyboard. The strip is one tab stop. Left/Right move a
/// highlight among the clickable items, and Enter/Space activates it. The arrow keys alone never
/// navigate. When the trail is wider than the control, the leading items fold into an ellipsis
/// ("…"). Its flyout lists them nearest first, and picking one raises on_item_click() with that
/// item's own index.
///
/// The control never changes its own items. A click is a request, and the handler decides what
/// to do, usually cutting the trail back to the clicked item.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::BreadcrumbControl* bar = stack->add(std::make_unique<ui::BreadcrumbControl>(
///     std::vector<std::string>{"Home", "Documents", "Design", "Northwind"}));
/// bar->on_item_click([bar](std::size_t index)
/// {
///     std::vector<std::string> items = bar->items();
///     items.resize(index + 1); // navigate up: drop everything after the clicked item
///     bar->set_items(std::move(items));
/// });
/// window.set_content(std::move(stack));
///
/// // Declaratively, reporting the clicked folder:
/// ui::Property<std::string> location{"Design"};
/// ui::Control<ui::Breadcrumb> crumbs;
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::Breadcrumb{
///             .items         = {"Home", "Documents", "Design"},
///             .on_item_click = [&](std::size_t index) { location = crumbs->items()[index]; },
///             .control       = crumbs,
///         },
///         ui::Text{.text = location},
///     },
/// });
/// @endcode
class BreadcrumbControl final : public Element, private ValueProvider
{
public:
    /// An empty trail; it draws nothing and is not focusable.
    BreadcrumbControl();
    /// A trail of `items`, root first; the last is the current location.
    explicit BreadcrumbControl(std::vector<std::string> items);
    /// Destroys the ellipsis flyout with the control.
    ~BreadcrumbControl() override;
    /// Non-copyable, like every Element.
    BreadcrumbControl(const BreadcrumbControl&) = delete;
    /// Non-copyable, like every Element.
    BreadcrumbControl& operator=(const BreadcrumbControl&) = delete;

    // ---- items ----
    /// The trail, root first (UTF-8); the last entry is the current location.
    [[nodiscard]] const std::vector<std::string>& items() const
    {
        return items_;
    }
    /// Replaces the item list and clears the hover, press and keyboard highlight, closing an open
    /// flyout. Updates the layout. Safe to call from inside on_item_click().
    void set_items(std::vector<std::string> items);


    /// Fires with the index into items() when a clickable item is activated by a click,
    /// Enter/Space, or from the ellipsis flyout. Every item but the last is clickable. Replaces
    /// any previous handler.
    void on_item_click(Callback<void(std::size_t)> handler)
    {
        on_item_click_ = std::move(handler);
    }

    // ---- overflow ----
    /// Whether the leading items are folded into the ellipsis because the trail did not fit its
    /// width, as of the last arrange.
    [[nodiscard]] bool ellipsis_shown() const
    {
        return first_shown_ > 0;
    }
    /// Index of the first item drawn after the ellipsis (0 when nothing is folded); items before
    /// it live in the flyout.
    [[nodiscard]] std::size_t first_shown_item() const
    {
        return first_shown_;
    }
    /// An item's rect in window coordinates as of the last arrange, or nullopt when it is folded
    /// into the ellipsis or `index` is out of range.
    [[nodiscard]] std::optional<Rect> item_bounds(std::size_t index) const;
    /// The ellipsis item's rect while it shows; nullopt when nothing is folded.
    [[nodiscard]] std::optional<Rect> ellipsis_bounds() const;
    /// Whether the ellipsis flyout listing the folded items is open.
    [[nodiscard]] bool flyout_open() const
    {
        return flyout_open_;
    }
    /// Opens or closes the ellipsis flyout (a click on the ellipsis toggles it). Opening does
    /// nothing while no items are folded or the control is disabled.
    void set_flyout_open(bool open);

    // ---- focus ----
    /// Whether the strip holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Focusable while the control is enabled and has a clickable item, which takes at least two
    /// items since the last one is never clickable.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && items_.size() > 1;
    }

    // ---- automation ----
    /// A Group: the items are plain strings with no automation nodes of their own.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Group;
    }

protected:
    // ---- layout ----
    /// The whole trail's natural width (items, chevrons) and one row's height; it never wraps.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Decides how many leading items fold into the ellipsis so the rest fit `final_rect`.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- render ----
    /// Draws the items, chevrons, the ellipsis and the keyboard focus ring. Hover and press change
    /// the text color, and in some themes the background.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- input ----
    /// Button-style clicks: a press on an item followed by a release anywhere activates it. A
    /// click on the ellipsis toggles the flyout.
    bool pointer_override(const PointerEvent& e) override;


private:
    // Left/Right walk the crumbs.
    // Claimed so Window's directional focus navigation leaves them alone
    // (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight ||
               (flyout_open_ && (key == Key::ArrowUp || key == Key::ArrowDown));
    }
    // Enter invokes the focused item (Element::key_override()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // The ellipsis flyout: a light-dismiss popup of the folded items, nearest-first, below the
    // ellipsis item, while flyout_open() and the ellipsis shows.
    void sync_popup();
    // The ellipsis item's stand-in index for the keyboard highlight, distinct from every real
    // item index and from NoIndex.
    static constexpr std::size_t Ellipsis = static_cast<std::size_t>(-2);

    friend class BreadcrumbFlyoutList;

    // The last item is the current location, not a link.
    [[nodiscard]] bool is_clickable(std::size_t index) const
    {
        return index + 1 < items_.size();
    }
    [[nodiscard]] std::size_t hit_test(Point p) const; // clickable items and Ellipsis; detail::NoIndex elsewhere
    void                      activate(std::size_t index);
    /// The keyboard highlight's stops in drawn order: the ellipsis, then the shown clickable items.
    [[nodiscard]] std::vector<std::size_t> stops() const;

    std::vector<std::string> items_;

    std::size_t hovered_index_ = detail::NoIndex;
    std::size_t pointer_down_index_ = detail::NoIndex; // Button-style: press must originate here, release anywhere counts
    std::size_t focused_item_index_ = detail::NoIndex; // roving keyboard highlight among clickable items (or Ellipsis)
    std::size_t flyout_cursor_      = detail::NoIndex; // the keyboard's item inside the open flyout
    bool        key_down_           = false;

    // Set by measure(), read by hit_test() and render().
    mutable float              row_height_ = 0.0f;
    mutable std::vector<float> item_widths_; // per item, text + horizontal padding only (chevrons are separate)
    mutable float              ellipsis_width_ = 0.0f; // the ellipsis item's button, without its chevron

    // From arrange_override(): the first item not folded into the ellipsis.
    std::size_t first_shown_ = 0;

    std::unique_ptr<BreadcrumbFlyoutList> flyout_;
    bool                                  flyout_open_ = false;

    Callback<void(std::size_t)> on_item_click_;

protected:
    void enabled_changed() override;
    /// Left/Right move the keyboard highlight and Enter/Space activates it. Up, Down and Escape
    /// work in the open flyout.
    bool key_override(const KeyEvent& e) override;
    /// Called by Window. Gaining focus puts the keyboard highlight on the ellipsis if it shows,
    /// else on the first shown clickable item. Losing focus clears the highlight. Redraws.
    void focus_changed(bool focused) override;

protected:
    /// Around the focused item, or the ellipsis.
    [[nodiscard]] Rect focus_ring_rect(const Theme& theme) const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value = this;
    }
    // The current location (the last item), or empty when there are no items.
    [[nodiscard]] std::string text_value() const override
    {
        return items_.empty() ? std::string{} : items_.back();
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

/// A navigation trail (Home > Folder > File). Builds a ui::BreadcrumbControl. The last item is
/// the current location and is plain text. Every earlier item fires `.on_item_click` when
/// clicked or Enter/Space-activated.
///
/// Example:
/// @code
/// ui::Control<ui::Breadcrumb> trail;
///
/// window.set_content(ui::Breadcrumb{
///     .items         = {"Home", "Documents", "Design", "Northwind"},
///     .on_item_click = [&](std::size_t index)
///     {
///         // Navigate up: drop everything after the clicked folder.
///         std::vector<std::string> items = trail->items();
///         items.resize(index + 1);
///         trail->set_items(std::move(items));
///     },
///     .control = trail,
/// });
/// @endcode
struct Breadcrumb
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Breadcrumb>` points at.
    using control_type = BreadcrumbControl;

    // ---- properties ----
    /// The trail, root first; the last entry is the current location. Bind it to a property and
    /// assign a shorter trail to go back up.
    Binding<std::vector<std::string>> items{};

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
    /// Called with the index of an activated item. Never fires for the last (current) item.
    Callback<void(std::size_t)> on_item_click{};
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
    ControlOut<Breadcrumb> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::BreadcrumbControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

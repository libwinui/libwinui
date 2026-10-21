// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <vector>

#include <cstddef>

namespace ui
{

/// One top-level entry of a Menu bar: the header title and the items of the menu that drops
/// down from it.
///
/// Example:
/// @code
/// auto menu = std::make_unique<ui::MenuBarControl>();
/// ui::MenuBarItem file{.text = "File", .items = {
///     {.text = "Open", .shortcut = ui::Ctrl + ui::Key::O, .on_click = [&] { open_file(); }},
///     ui::MenuItem::separator(),
///     {.text = "Exit", .on_click = [&] { quit(); }},
/// }};
/// menu->set_items({std::move(file)});
/// @endcode
struct MenuBarItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The header title shown in the bar.
    std::string text;
    /// The menu's entries; they may be toggles, radio items, separators and submenus.
    std::vector<MenuItem> items;
};

class MenuBarHeaderAutomation; // MenuBar.cpp: one header as automation sees it

/// A horizontal menu bar: a row of headers, each dropping down a cascading
/// MenuFlyout-style menu that floats in the overlay layer under the header's left edge.
///
/// The bar is one tab stop. Left/Right move between headers and wrap around. Down, Enter and
/// Space open the focused header's menu with its first item focused. While a menu is open the
/// bar keeps focus and passes the keys on to the menu. Every item's shortcut fires app-wide,
/// whether its menu is open or not. The declarative form is ui::MenuBar.
///
/// Example:
/// @code
/// bool show_status_bar = true;
/// auto menu = std::make_unique<ui::MenuBarControl>();
/// menu->set_items({
///     ui::MenuBarItem{.text = "File", .items = {
///         {.text = "Open", .shortcut = ui::Ctrl + ui::Key::O, .on_click = [&] { open_file(); }},
///         ui::MenuItem::separator(),
///         {.text = "Exit", .on_click = [&] { quit(); }},
///     }},
///     ui::MenuBarItem{.text = "View", .items = {
///         ui::MenuItem::toggle("Status bar", /*checked=*/true,
///                                       [&](bool on) { show_status_bar = on; }),
///     }},
/// });
///
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::MenuBarControl* bar = stack->add(std::move(menu)); // observer; the stack owns it
/// window.set_content(std::move(stack));
///
/// // Or declaratively:
/// window.set_content(ui::MenuBar{ .items = {
///     { .text = "File", .items = { { .text = "Open", .on_click = [&] { open_file(); } },
///                                  ui::MenuItem::separator(),
///                                  { .text = "Exit", .on_click = [&] { quit(); } } } },
/// }});
/// @endcode
class MenuBarControl final : public Element
{
public:
    /// An enabled, empty bar; it is not focusable until it has items.
    MenuBarControl();
    ~MenuBarControl() override;
    /// Moves a bar that has not been parented yet.
    MenuBarControl(MenuBarControl&&) noexcept;
    MenuBarControl& operator=(MenuBarControl&&) = delete;

    // ---- content ----
    /// The menus, in header order. Toggle and radio items' is_checked() reflect clicks made in
    /// the open menu and the properties their `checked` is bound to, so read them back from here.
    [[nodiscard]] const std::vector<MenuBarItem>& items() const
    {
        return items_;
    }
    /// Replaces the menus, closes any that is open and lays out the header row again.
    void set_items(std::vector<MenuBarItem> items);


    /// Which header's menu is open; unset when none is.
    [[nodiscard]] std::optional<std::size_t> open_index() const
    {
        return detail::optional_index(open_index_);
    }
    /// The item holding focus in the open menu (an index into its top level); unset when there is
    /// none.
    [[nodiscard]] std::optional<std::size_t> active_command() const;
    /// The header the keyboard is on (Left/Right move it); 0 for a fresh bar.
    [[nodiscard]] std::size_t focused_header() const
    {
        return focused_header_;
    }

    /// Opens header `index`'s menu, as a click on it would, closing any other. With
    /// `from_keyboard` the menu's first focusable item is focused, as Down/Enter/Space do.
    /// Does nothing for an out-of-range index or a disabled bar; a header with an empty menu only
    /// takes the keyboard focus.
    void open(std::size_t index, bool from_keyboard = false);
    /// Closes the open menu and its submenus. The menu fades out over the theme's close duration.
    void close();

    // ---- focus ----
    /// Whether the bar holds keyboard focus; the focus ring is then drawn around focused_header().
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// One tab stop for the whole bar, while it is enabled and has at least one header.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && !items_.empty();
    }
    /// Every menu's shortcuts, open or not.
    bool handle_global_shortcut(const KeyEvent& e) override;
    /// Alt or F10 alone moves keyboard focus to the first header. Alt with a letter opens the menu
    /// whose header starts with that letter, ignoring case, with its first item focused. Escape and
    /// running an item give focus back (Window::end_menu_access()).
    bool take_menu_access(char32_t letter) override;

    // ---- overlay / popup ----
    /// The menu's opacity: fully opaque while it slides in, then fading out as it closes.
    [[nodiscard]] float transition_opacity() const override;

    // ---- automation ----
    /// AutomationRole::MenuBar.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::MenuBar;
    }
    /// One MenuItem node per header, which expands to open its menu.
    void automation_children(std::vector<Element*>& out) const override;
    /// The open menu belongs to its header's node.
    [[nodiscard]] Element* automation_parent_of(const Element& child) const override;
    /// The open menu's item the keyboard is on, else the header it is on.
    [[nodiscard]] Element* automation_focus() const override;

protected:
    // ---- layout ----
    /// The header row. Each header is its title plus padding and margin, and the bar is at least
    /// 40 DIPs tall. An open menu floats and never changes this size.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Keeps an open menu hanging from its header.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Hover highlights a header; a click opens its menu, and a second click on the same header
    /// closes it. While a menu is open, moving onto another header opens that one instead.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the header row: subtle header backgrounds, the open menu's header pressed-in, and the
    /// focus ring around the focused header. The menu itself is a popup the window draws.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the menu's open slide, submenu hover delays and the close fade.
    void tick_override(Duration delta) override;
    /// Clears the bar's dirty flags and those of the floating menu with it.
    void clear_dirty_override(DirtyFlags flags) override;


private:
    // All four arrows belong to the bar and its menus, never to directional focus navigation.
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    // Enter opens a menu or invokes its item, so the Window must not treat it as a default-button
    // press.
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    friend class MenuBarHeaderAutomation;

    // The node of header `index`, made on first use and kept for the bar's life.
    [[nodiscard]] MenuBarHeaderAutomation* header_automation(std::size_t index) const;

    // The floating menu presenter, created on first use and owned by the bar.
    detail::MenuPresenter& presenter();
    // Opens, updates or closes the menu's popup: open while the menu shows and through its
    // 83 ms close fade, hanging from the open header's own rect (inside its margin).
    void sync_popup();
    class Host;

    [[nodiscard]] Rect        header_bounds(std::size_t index) const; // the slot, margin included
    [[nodiscard]] Rect        header_visual(std::size_t index) const; // inside the margin
    [[nodiscard]] std::size_t hit_test_header(Point p) const;
    [[nodiscard]] ButtonState header_state(std::size_t index) const;
    [[nodiscard]] double      close_ms() const;
    void                      step_menu(int step);

    std::vector<MenuBarItem> items_;

    std::size_t open_index_          = detail::NoIndex;
    std::size_t closing_index_       = detail::NoIndex;
    std::size_t focused_header_      = 0;
    std::size_t hovered_header_      = detail::NoIndex;
    std::size_t pointer_down_header_ = detail::NoIndex;
    double      closing_ms_          = -1.0; // the close fade's clock; negative when not fading

    mutable std::vector<float> header_widths_;
    mutable float              header_height_ = 0.0f;
    // measure(): an item's padding and margin, the theme's (an era bar has its own).
    mutable Thickness header_padding_{};
    mutable Thickness header_margin_{};

    std::unique_ptr<Host>                  host_;
    std::unique_ptr<detail::MenuPresenter> presenter_;
    std::unique_ptr<detail::MenuChecks>    checks_; // every menu's bound `checked`, submenus too

    mutable std::vector<std::unique_ptr<MenuBarHeaderAutomation>> header_automation_;

protected:
    void enabled_changed() override;
    /// Called by the Window as focus arrives or leaves; losing focus closes the open menu.
    void focus_changed(bool focused) override;
    /// Left/Right move between headers. With a menu open, they move into the neighboring menu
    /// when the open menu has no use for the key. Down, Enter and Space open the focused header's
    /// menu, and while a menu is open its keys go to it.
    bool key_override(const KeyEvent& e) override;

protected:
    /// Around the focused header.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return focused_header_ < items_.size() ? header_visual(focused_header_) : Rect{};
    }
};

/// A menu bar. Builds a ui::MenuBarControl. Each menu is a list of ui::MenuItem, so its items may
/// be toggles, radio items, separators or submenus with icons and shortcuts. Every item's shortcut
/// fires app-wide, whether its menu is open or not.
///
/// Menu items live in a std::vector built from a brace list, which only copies, so
/// ui::MenuItem::on_click is a copyable std::function. A handler must not capture a move-only value.
///
/// Example:
/// @code
/// ui::Property<bool> status_bar{true};
/// window.set_content(ui::VerticalStack{ .content = {
///     ui::MenuBar{ .items = {
///         { .text  = "File",
///           .items = { { .text = "Open", .shortcut = ui::Ctrl + ui::Key::O,
///                        .on_click = [&] { open_file(); } },
///                      ui::MenuItem::separator(),
///                      { .text = "Exit", .on_click = [&] { quit(); } } } },
///         { .text  = "View",
///           .items = { ui::MenuItem::toggle("Status bar", ui::bind(status_bar)) } },
///     }},
///     ui::Text{ .text = "Document" },
/// }});
/// @endcode
struct MenuBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<MenuBar>` points at.
    using control_type = MenuBarControl;

    // ---- properties ----
    /// The bar's headers, left to right, each with the menu it drops down.
    Binding<std::vector<MenuBarItem>> items{};

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
    ControlOut<MenuBar> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::MenuBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

class ButtonControl;
class ToolTipControl;
class NavFlyoutList; // NavigationShell.cpp: the rows of a children or overflow flyout
class NavPaneButtonContent;

/// Where a NavigationShell puts its items.
enum class NavigationShellPaneDisplayMode
{
    /// Expanded from expanded_mode_threshold_width() on, Minimal below
    /// compact_mode_threshold_width(), Compact between.
    Auto,
    /// Always Expanded: an inline pane, a compact strip while closed.
    Left,
    /// The items in a 48-DIP bar across the top; what does not fit goes behind "More".
    Top,
    /// Always Compact: an icon strip, opening over the content.
    LeftCompact,
    /// Always Minimal: no pane until the toggle button opens one over the content.
    LeftMinimal,
};

/// What a NavigationShell's pane is doing right now.
enum class NavigationShellDisplayMode
{
    /// No pane showing while closed. An open pane overlays the content and closes on a click
    /// outside it.
    Minimal,
    /// An icon strip (compact_pane_length() wide) while closed; an open pane overlays the content.
    Compact,
    /// An inline pane beside the content, shrinking to the strip when closed.
    Expanded,
};

/// Whether a NavigationShell selects the item the arrow keys land on.
enum class NavigationShellSelectionFollowsFocus
{
    /// The default: the arrow keys move a cursor, Enter/Space select.
    Disabled,
    /// Whatever item the arrow keys land on is selected at once.
    Enabled,
};

/// What a NavEntry is: an item, a section header or a separator.
enum class NavEntryKind
{
    /// A selectable, invokable row.
    Item,
    /// A section title showing the entry's text; folded away while the pane is closed compact.
    Header,
    /// A horizontal rule between groups of items.
    Separator,
};

/// One navigation entry: the text in the pane, the content shown while it is selected, and any
/// nested items it groups. It is plain data, not an Element, but it owns its `content` subtree.
///
/// That makes it move-only, so a `std::vector<NavEntry>` is built by pushing into it. For a
/// braced list use `ui::NavItem`/`ui::NavList` from this header.
///
/// Example:
/// @code
/// auto                      nav = std::make_unique<ui::NavigationShellControl>();
/// std::vector<ui::NavEntry> items;
///
/// ui::NavEntry home{"Home"};
/// home.icon    = ui::symbol_glyph(ui::Symbol::Home);
/// home.content = std::make_unique<ui::TextControl>("Welcome");
/// items.push_back(std::move(home));
///
/// ui::NavEntry mail{"Mail"};
/// mail.icon       = ui::symbol_glyph(ui::Symbol::Mail);
/// mail.expanded   = true;
/// mail.info_badge = std::make_unique<ui::InfoBadgeControl>();
/// mail.children.push_back(ui::NavEntry{"Inbox"});
/// mail.children.push_back(ui::NavEntry{"Sent"});
/// items.push_back(std::move(mail));
///
/// nav->set_items(std::move(items)); // pre-order indices: Home 0, Mail 1, Inbox 2, Sent 3
/// @endcode
struct NavEntry
{
    /// The row's text; a header's title. Also the tooltip and the
    /// automation value when the item is selected.
    std::string text{};
    /// The content shown while this item is selected. May be null, and then the view's own
    /// content (NavigationShellControl::set_content) is shown instead. That is usually a
    /// NavigationFrame when the items carry no pages at all.
    std::unique_ptr<Element> content{};
    /// Nested items. A non-empty list makes this row a collapsible group with a chevron.
    std::vector<NavEntry> children{};
    /// Whether a group shows its children inline in the left pane. False by default.
    bool expanded = false;
    /// Whether invoking the row selects it. False makes the row a pure heading: it still expands
    /// and collapses, but never becomes the selection.
    bool selects_on_invoke = true;
    /// The icon, drawn at 16 DIPs in the row's icon column: a Symbol, a glyph, a path or a picture.
    IconSource icon{};
    /// A badge drawn between the text and the chevron.
    std::unique_ptr<InfoBadgeControl> info_badge{};
    /// An item, a section header or a separator. A header shows `text` and a separator nothing;
    /// neither is ever selected, invoked or given the keyboard cursor.
    NavEntryKind kind = NavEntryKind::Item;
    /// The row's tooltip. Empty shows the text, and only while the pane is closed compact. A
    /// non-empty one shows in every mode.
    std::string tool_tip{};
    /// An animated icon, drawn instead of `icon` in the pane's rows and the top bar in the row's
    /// text color. Its state follows the row's: PointerOver while hovered, Pressed while pressed,
    /// Normal otherwise. `icon` still names the row's icon everywhere else (the overflow and
    /// children flyouts), so set it to the animated icon's fallback glyph.
    std::unique_ptr<AnimatedIconControl> animated_icon{};
    /// What identifies the item in selected_key() and the selection events. Empty uses `text`.
    /// "settings" is taken by the Settings item.
    std::string key{};
};

/// What NavigationShellControl::on_expanded_changed() gets: the group whose expansion changed, and
/// whether it is expanded now.
struct NavigationShellExpandedChangedArgs
{
    std::size_t index    = 0;     ///< The group's pre-order index.
    bool        expanded = false; ///< True when it opened.
};

/// A navigation view: a pane of navigation items (left, compact, minimal or a top bar) beside
/// a content area that shows the selected item's page.
///
/// Items are NavEntry values. The selection is reported as keys: an item's NavEntry::key, else
/// its text, and "settings" for the Settings item. The positional calls (set_expanded(),
/// text_at(), item_bounds()) take a pre-order index over the menu items, then the footer items,
/// with Settings for the Settings item. A selected item shows its own `content`, or the view's
/// content from set_content() when it has none. In Auto mode the pane adapts to the control's
/// width. It opens and closes on its toggle button, and closes on a click outside when it
/// overlays the content. It can host an AutoSuggestBox, a pane header, custom content and a
/// footer, all owned by the view. The items are one tab stop with a keyboard cursor. The
/// declarative form is ui::NavigationShell with ui::NavItem.
///
/// Example:
/// @code
/// auto nav = std::make_unique<ui::NavigationShellControl>();
/// nav->set_pane_display_mode(ui::NavigationShellPaneDisplayMode::Left);
/// nav->set_pane_title("My app");
/// nav->add_item("Home", std::make_unique<ui::TextControl>("Welcome home"));
/// nav->add_item("Library", std::make_unique<ui::TextControl>("Your library"));
/// nav->on_selection_changed([](const std::string& key) {
///     if (key == ui::NavigationShellControl::SettingsKey)
///         std::println("open the settings page");
/// });
/// ui::NavigationShellControl* view = window.set_content(std::move(nav));
/// view->set_header(view->selected_key()); // "Home": the first item is selected, keyed by its text
///
/// // Or declaratively, with one shared content region driven by the selection:
/// ui::Property<std::string> page;
/// window.set_content(ui::NavigationShell{
///     .selected_key      = page, // "home" at first, then whatever the user picks
///     .pane_display_mode = ui::NavigationShellPaneDisplayMode::Top,
///     .content           = ui::Text{.text = page},
///     .items             = {ui::NavItem{.text = "Home", .key = "home", .icon = ui::symbol_glyph(ui::Symbol::Home)},
///                           ui::NavItem{.text = "Mail", .key = "mail", .icon = ui::symbol_glyph(ui::Symbol::Mail)}},
/// });
/// @endcode
class NavigationShellControl final : public Element, private ValueProvider
{
    /// Forwards to the owned elements and the active content.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;

public:
    /// The Settings item's index in the positional calls (text_at(), item_bounds(),
    /// set_selected_index()).
    static constexpr std::size_t Settings = static_cast<std::size_t>(-2);
    /// The Settings item's key, as selected_key() reports it and set_selected_key() takes it.
    static constexpr std::string_view SettingsKey = "settings";

    /// An empty view in Auto mode with an open pane, the Settings item visible and the back button
    /// shown but disabled; it creates its own back, toggle, close and search buttons.
    NavigationShellControl();
    ~NavigationShellControl() override;
    NavigationShellControl(const NavigationShellControl&)            = delete;
    NavigationShellControl& operator=(const NavigationShellControl&) = delete;

    // ---- items ----

    /// Replaces the menu items and resets the selection to the first selectable item. A selected
    /// Settings item stays selected. The keyboard cursor and the selection indicator's animation
    /// reset too.
    void set_items(std::vector<NavEntry> items);
    /// Items at the bottom of the pane (the right of the top bar), above the
    /// Settings item, in the same selection model. Their indices follow every menu item's.
    /// Keeps the selection when it was a menu item or Settings.
    void set_footer_items(std::vector<NavEntry> items);

    /// Appends a leaf at the top level of the menu items, taking ownership of its content, and
    /// returns a non-owning observer to the content. The first item added becomes selected. Use
    /// set_items() for nested items.
    template <typename T>
    T* add_item(std::string text, std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "NavigationShell content must be an Element");
        T* observer = content.get();
        if (observer != nullptr)
        {
            set_parent_of(*observer, this);
        }
        NavEntry entry{};
        entry.text    = std::move(text);
        entry.content = std::move(content);
        entries_.push_back(std::move(entry));
        rebuild_rows();
        if (selected_index_ == detail::NoIndex)
        {
            selected_index_ = 0;
            cursor_index_   = 0;
        }
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }

    /// The content region used by any selected item whose own `content` is null, usually
    /// a NavigationFrame. Takes ownership and hands back a non-owning observer.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "NavigationShell content must be an Element");
        T* observer = content.get();
        adopt(content_, std::move(content));
        return observer;
    }

    /// Every item in the tree, menu and footer, collapsed ones included. This is the pre-order
    /// index space of selected_index() and set_expanded().
    [[nodiscard]] std::size_t item_count() const
    {
        return all_.size();
    }
    /// How many of those are footer items (the last ones).
    [[nodiscard]] std::size_t footer_item_count() const
    {
        return all_.size() - footer_begin_;
    }
    /// How many menu rows are currently drawn in the pane, i.e. not hidden inside a collapsed
    /// ancestor. Footer rows are not counted.
    [[nodiscard]] std::size_t visible_row_count() const
    {
        return menu_rows_.size();
    }
    /// The text of the item at pre-order `index` ("Settings" for Settings), or an empty
    /// string if out of range.
    [[nodiscard]] const std::string& text_at(std::size_t index) const;
    /// Changes the text of the item at pre-order `index`; updates the layout and redraws. An item without a
    /// NavEntry::key is keyed by its text, so its key changes too. Out of range, and Settings, are
    /// no-ops.
    void set_text(std::size_t index, std::string text);
    /// The key of the item at pre-order `index`: its NavEntry::key, else its text; "settings"
    /// for Settings; empty out of range.
    [[nodiscard]] const std::string& key_at(std::size_t index) const;
    /// The pre-order index of the first item with `key`, Settings for "settings"; unset when no item
    /// has it.
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view key) const;
    /// The badge of the item at pre-order `index`, or null.
    [[nodiscard]] InfoBadgeControl* info_badge_at(std::size_t index) const;
    /// Replaces (or with null, removes) the badge of the item at pre-order `index`, and returns
    /// the new one as an observer. Out of range is a no-op returning null.
    InfoBadgeControl* set_info_badge(std::size_t index, std::unique_ptr<InfoBadgeControl> badge);
    /// Changes NavEntry::selects_on_invoke of the item at pre-order `index`. Out of range is a
    /// no-op.
    void set_selects_on_invoke(std::size_t index, bool selects);
    /// The item drawn at menu row `row` (pre-order index); unset if out of range.
    [[nodiscard]] std::optional<std::size_t> item_at_row(std::size_t row) const
    {
        return row < menu_rows_.size() ? detail::optional_index(menu_rows_[row].item) : std::nullopt;
    }

    // ---- selection ----

    /// The selected item's key ("settings" for Settings), or empty when nothing is selected.
    [[nodiscard]] const std::string& selected_key() const
    {
        return key_at(selected_index_);
    }
    /// Selects the first item with `key`; "settings" selects the Settings item and an empty key
    /// clears the selection. A key no item has is a no-op, otherwise as set_selected_index().
    void set_selected_key(std::string_view key);
    /// The selected item's pre-order index, or Settings; unset when nothing is selected.
    [[nodiscard]] std::optional<std::size_t> selected_index() const
    {
        return detail::optional_index(selected_index_);
    }
    /// Selects the item at pre-order `index`, clamped to the item list. Settings selects the
    /// Settings item and unset clears the selection. Does nothing, and fires nothing, if the item
    /// is already selected, is a heading (`selects_on_invoke == false`), a header or a separator,
    /// or is Settings while it is hidden. Either way the left pane scrolls the item into view on
    /// the next layout, after laying out any expansion made just before.
    void set_selected_index(std::optional<std::size_t> index);
    /// Whether the Settings item is the selection.
    [[nodiscard]] bool settings_selected() const
    {
        return selected_index_ == Settings;
    }
    /// Fired with the new key whenever the selection changes (a click, the keyboard,
    /// set_selected_key(), set_selected_index()); empty when it was cleared. A call that lands on
    /// the already-selected item does not fire it.
    void on_selection_changed(Callback<void(const std::string&)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }
    /// Fired with the item's key on every click or Enter/Space on an item, Settings included,
    /// whether or not it changes the selection. It is raised before the selection moves.
    void on_item_invoked(Callback<void(const std::string&)> handler)
    {
        on_item_invoked_ = std::move(handler);
    }

    /// Whether moving the keyboard cursor also selects. Disabled by default.
    [[nodiscard]] NavigationShellSelectionFollowsFocus selection_follows_focus() const
    {
        return selection_follows_focus_;
    }
    /// Takes effect on the next cursor move; it does not select the item under the cursor now.
    void set_selection_follows_focus(NavigationShellSelectionFollowsFocus value)
    {
        selection_follows_focus_ = value;
    }

    /// Whether the group at pre-order `index` is expanded; false for a leaf or out of range.
    [[nodiscard]] bool expanded(std::size_t index) const;
    /// Expands or collapses the group at `index`. A no-op on an item with no children.
    void set_expanded(std::size_t index, bool expanded);
    /// Fired whenever a group's expansion changes, by click, chevron or keyboard.
    void on_expanded_changed(Callback<void(const NavigationShellExpandedChangedArgs&)> handler)
    {
        on_expanded_changed_ = std::move(handler);
    }

    // ---- display mode ----

    /// Where the items go. Auto (the default) adapts to the control's width.
    [[nodiscard]] NavigationShellPaneDisplayMode pane_display_mode() const
    {
        return pane_display_mode_;
    }
    /// Switching between LeftMinimal and Left opens or closes the pane to match; switching to Top
    /// closes it.
    void set_pane_display_mode(NavigationShellPaneDisplayMode mode);
    /// What the pane resolved to at the last arrange (Minimal until the first one).
    [[nodiscard]] NavigationShellDisplayMode display_mode() const
    {
        return display_mode_;
    }
    /// Raised from arrange when display_mode() resolves to a new value,
    /// including the first time.
    void on_display_mode_changed(Callback<void(NavigationShellDisplayMode)> handler)
    {
        on_display_mode_changed_ = std::move(handler);
    }
    /// The width below which Auto is Minimal, in DIPs; 641 by default.
    [[nodiscard]] float compact_mode_threshold_width() const
    {
        return compact_threshold_;
    }
    /// Updates the layout; the new threshold applies from the next width change.
    void set_compact_mode_threshold_width(float width)
    {
        compact_threshold_ = width;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The width from which Auto is Expanded, in DIPs; 1008 by default.
    [[nodiscard]] float expanded_mode_threshold_width() const
    {
        return expanded_threshold_;
    }
    /// Updates the layout; the new threshold applies from the next width change.
    void set_expanded_mode_threshold_width(float width)
    {
        expanded_threshold_ = width;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- the pane ----

    /// Whether the pane is open. Opening raises on_pane_opening and on_pane_opened; closing raises
    /// on_pane_closing (cancelable only for a light dismiss) and on_pane_closed. In Top mode the
    /// pane never opens.
    [[nodiscard]] bool is_pane_open() const
    {
        return pane_open_;
    }
    /// Opens or closes the pane with the same slide a SidePane uses. Closing an Expanded pane this
    /// way counts as a user close, so growing back into Expanded does not reopen it.
    void set_pane_open(bool open);
    /// Raised when the pane starts to open, before its transition.
    void on_pane_opening(Callback<void()> handler)
    {
        on_pane_opening_ = std::move(handler);
    }
    /// Raised once the pane is open, as soon as its state flips (not after the slide).
    void on_pane_opened(Callback<void()> handler)
    {
        on_pane_opened_ = std::move(handler);
    }
    /// Raised before the pane closes; return ui::Cancel to keep it open. Only a light dismiss can
    /// be canceled. The toggle button and set_pane_open(false) always close it.
    void on_pane_closing(Callback<Decision()> handler)
    {
        on_pane_closing_ = std::move(handler);
    }
    /// Raised once the pane is closed, as soon as its state flips (not when canceled).
    void on_pane_closed(Callback<void()> handler)
    {
        on_pane_closed_ = std::move(handler);
    }

    /// The open pane's width in DIPs, 320 by default, clamped to the control's own width.
    [[nodiscard]] float pane_width() const
    {
        return pane_width_;
    }
    /// Updates the layout. The width is clamped only when the pane is arranged.
    void set_pane_width(float width)
    {
        pane_width_ = width;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The closed strip's width in DIPs, 48 by default, which is also the width of the toggle and
    /// search buttons.
    [[nodiscard]] float compact_pane_length() const
    {
        return compact_pane_length_;
    }
    /// Updates the layout with the new strip width.
    void set_compact_pane_length(float length)
    {
        compact_pane_length_ = length;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether the pane shows (true). False hides the pane and its toggle button altogether.
    [[nodiscard]] bool is_pane_visible() const
    {
        return pane_visible_;
    }
    /// Shows or hides the pane; the layout adapts to it on the next arrange.
    void set_pane_visible(bool visible);
    /// Whether the hamburger button shows (true).
    [[nodiscard]] bool is_pane_toggle_button_visible() const
    {
        return toggle_visible_;
    }
    /// Shows or hides the hamburger button and updates the layout.
    void set_pane_toggle_button_visible(bool visible);
    /// The pane title, shown beside the hamburger while the pane is open.
    [[nodiscard]] const std::string& pane_title() const
    {
        return pane_title_;
    }
    /// Sets the title (on the toggle button, or at the top bar's left in Top mode) and updates the
    /// layout.
    void set_pane_title(std::string title);

    /// Whether the back button shows; Auto, the default, shows it.
    [[nodiscard]] AutoVisibility back_button_visible() const
    {
        return back_visible_;
    }
    /// Shows or hides the back button and updates the layout.
    void set_back_button_visible(AutoVisibility visible);
    /// Whether the back button is enabled; false by default. A visible back button is disabled
    /// until this is set.
    [[nodiscard]] bool is_back_enabled() const
    {
        return back_enabled_;
    }
    /// Enables or disables the back button; typically bound to a frame's can_go_back().
    void set_back_enabled(bool enabled);
    /// Raised when the back button is clicked.
    void on_back_requested(Callback<void()> handler)
    {
        on_back_requested_ = std::move(handler);
    }

    /// Whether the Settings item shows at the end of the footer items (true).
    [[nodiscard]] bool is_settings_visible() const
    {
        return settings_visible_;
    }
    /// Shows or hides the Settings item; hiding it while selected clears the selection.
    void set_settings_visible(bool visible);

    /// The search box slot: any Element, placed in the pane under the toggle button (in
    /// the top bar's right in Top mode). Null removes it.
    template <typename T>
    T* set_auto_suggest_box(std::unique_ptr<T> box)
    {
        T* observer = box.get();
        adopt(auto_suggest_box_, std::move(box));
        return observer;
    }
    /// The search box slot's element (owned by the view), or null.
    [[nodiscard]] Element* auto_suggest_box() const
    {
        return auto_suggest_box_.get();
    }
    /// The pane header: any Element, under the toggle button, beside it in Top mode.
    template <typename T>
    T* set_pane_header_content(std::unique_ptr<T> header)
    {
        T* observer = header.get();
        adopt(pane_header_, std::move(header));
        return observer;
    }
    /// The pane header element (owned by the view), or null.
    [[nodiscard]] Element* pane_header_content() const
    {
        return pane_header_.get();
    }
    /// Custom pane content: any Element, above the menu items, after them in Top mode.
    template <typename T>
    T* set_pane_custom_content(std::unique_ptr<T> content)
    {
        T* observer = content.get();
        adopt(pane_custom_content_, std::move(content));
        return observer;
    }
    /// The custom pane content element (owned by the view), or null.
    [[nodiscard]] Element* pane_custom_content() const
    {
        return pane_custom_content_.get();
    }
    /// The pane footer: any Element, above the footer items, before them in Top mode.
    template <typename T>
    T* set_pane_footer_content(std::unique_ptr<T> footer)
    {
        T* observer = footer.get();
        adopt(pane_footer_, std::move(footer));
        return observer;
    }
    /// The pane footer element (owned by the view), or null.
    [[nodiscard]] Element* pane_footer_content() const
    {
        return pane_footer_.get();
    }

    // ---- the content's header ----

    /// The content's header text, drawn 28px SemiBold above the content. Empty (the default)
    /// draws no header row.
    [[nodiscard]] const std::string& header() const
    {
        return header_;
    }
    /// Sets the header text and updates the content area's layout.
    void set_header(std::string header)
    {
        header_ = std::move(header);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether the header shows in every display mode (true). False shows it only in Minimal.
    [[nodiscard]] bool always_show_header() const
    {
        return always_show_header_;
    }
    /// Updates the content area's layout.
    void set_always_show_header(bool show)
    {
        always_show_header_ = show;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- appearance ----

    /// The height of one item row in the left pane, in DIPs: 40 by default (a 36-DIP pill in a
    /// 40-DIP slot).
    [[nodiscard]] float item_row_height() const
    {
        return item_row_height_;
    }
    /// Updates the pane's layout with the new row height.
    void set_item_row_height(float height)
    {
        item_row_height_ = height;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// How far the menu items are scrolled, in DIPs. Menu items taller than their viewport
    /// scroll on the wheel and through an overlay scrollbar.
    [[nodiscard]] float pane_scroll_offset() const
    {
        return pane_scroll_offset_;
    }
    /// Scrolls the menu items, clamped to 0..(content height - viewport height) from the last
    /// arrange; redraws when it moved.
    void set_pane_scroll_offset(float offset);


    // ---- geometry, from the last arrange (for callers placing things beside it) ----

    /// The part of the pane showing right now (animation included); empty in Top mode and while a
    /// Minimal pane is closed.
    [[nodiscard]] Rect pane_bounds() const;
    /// The content area: right of an inline pane, under the top bar in Top mode.
    [[nodiscard]] Rect content_bounds() const
    {
        return content_rect_;
    }
    /// The drawn rect of the item at pre-order `index` (Settings for Settings), or nullopt if
    /// it has no row showing (hidden in a collapsed group, in the overflow, or scrolled away).
    [[nodiscard]] std::optional<Rect> item_bounds(std::size_t index) const;
    /// The rect of the top bar's overflow ("More") button, or nullopt when nothing overflows.
    [[nodiscard]] std::optional<Rect> overflow_button_bounds() const;
    /// Whether the item at `index` is behind the overflow button in Top mode.
    [[nodiscard]] bool in_overflow(std::size_t index) const;
    /// Whether a children or overflow flyout is open.
    [[nodiscard]] bool flyout_open() const
    {
        return flyout_kind_ != FlyoutKind::None;
    }
    /// The internal buttons, for automation: back, pane toggle, close (Minimal, while
    /// open) and search (the search box's stand-in while closed compact).
    [[nodiscard]] ButtonControl* back_button() const
    {
        return back_button_;
    }
    /// The pane toggle (hamburger) button, owned by the view.
    [[nodiscard]] ButtonControl* toggle_button() const
    {
        return toggle_button_;
    }
    /// The close button a Minimal pane shows while open, owned by the view.
    [[nodiscard]] ButtonControl* close_button() const
    {
        return close_button_;
    }
    /// The search button shown in the search box's place while closed compact, owned by the view.
    [[nodiscard]] ButtonControl* search_button() const
    {
        return search_button_;
    }
    /// Closes an overlaying pane on Escape, as a light dismiss. The window calls it while focus is
    /// inside the pane.
    bool close_on_escape(const KeyEvent& e);

    // ---- focus ----
    /// Whether the items' tab stop holds keyboard focus (the cursor's pill then shows the ring).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The items are one tab stop whenever there is one to reach.
    [[nodiscard]] bool focusable() const override;
    /// That stop is the items alone. A press on the menu or footer rows, or on a slot in Top mode,
    /// focuses the view. A press on the hosted page reaches the controls there.
    [[nodiscard]] bool takes_focus_at(Point p) const override;
    /// In tab order: the back, close and toggle buttons, the pane header, the search button or
    /// search box, the pane custom content, the items (this), the pane footer, then the active
    /// content. While an overlaying pane is open, only the pane's own stops.
    void collect_focusables(std::vector<Element*>& out) override;

protected:
    /// Adds the active content, which for_each_child() leaves out.
    void build_visual_tree_override(VisualTree& tree) const override;

public:
    /// Passes native-control hosting on to the owned elements and the active content only. Native
    /// controls on an inactive item's page are hidden.
    void sync_native(NativeHost& host) override;
    // ---- automation ----
    /// Only the active content and the owned pane elements are automation children. The items are
    /// drawn rows, and the control exposes the selected item's text as a read-only value.
    void automation_children(std::vector<Element*>& out) const override;
    /// AutomationRole::List.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::List;
    }

protected:
    // ---- layout ----
    /// Fills `available` on whichever axes are bounded.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Resolves the display mode for the new width (raising on_display_mode_changed), then lays
    /// out the pane or top bar, the header and the content.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- render ----
    /// Draws the pane or top bar, the rows with their selection indicator, the header, then the
    /// active content; an overlaying pane is drawn over the content.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- input ----
    /// Hover, press and click on the rows, chevrons and overflow button. A click on the content
    /// while an overlaying pane is open closes the pane.
    bool pointer_override(const PointerEvent& e) override;
    /// Scrolls the pane; the content has had the turn first.
    bool wheel_override(const PointerEvent& e) override;
    /// The buttons over everything, the row tooltip, the pane's elements, then the content where
    /// nothing covers it.
    void hit_test_children(Point p, std::vector<Element*>& out) const override;
    /// The pane's overlay scrollbars.
    [[nodiscard]] bool covers_children_at(Point p) const override;

    // ---- tree/invalidation plumbing ----
    /// Clears the view's flags and those of every owned element.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Advances the pane's open/close transition, the indicator's slide and the animated icons.
    void tick_override(Duration delta) override;
    /// Asks the Window to move focus into an overlaying pane as it opens (and back out as it
    /// closes); the request is handed over once.
    [[nodiscard]] FocusRequest take_focus_request() override;


private:
    // Enter invokes the item under the cursor (Element::key_override()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // Up/Down move the cursor in the left pane (and in an open flyout); Left/Right collapse and
    // expand the group under it, or move the cursor along the top bar. Claimed so Window's
    // directional focus navigation leaves them alone (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const;
    // The view itself (for its children/overflow flyout), then the owned elements' and the
    // active content's live elements.
    void collect_live(std::vector<Element*>& out) override;
    // The children/overflow flyout: a light-dismiss popup of rows while one is open, placed by
    // place_flyout().
    void sync_popup();
    // The group's row, or the overflow button, the flyout hangs from.
    [[nodiscard]] Rect flyout_anchor() const;
    // Places the flyout itself: the overflow list right-aligned under "More", a children list
    // beside or under its group, kept inside the `client` rect.
    bool place_flyout(Element& content, Size client, const MeasureContext& context);
    friend class NavFlyoutList;

    /// The keyboard cursor's stand-in for the top bar's overflow button.
    static constexpr std::size_t OverflowStop = static_cast<std::size_t>(-3);

    // Rebuilt when the tree's shape or an expansion changes, never per frame.
    struct Row
    {
        std::size_t item;
        std::size_t depth;
    };

    // A top-bar slot: an item (or Settings), or the overflow button (item == None).
    struct TopSlot
    {
        std::size_t item;
        Rect        rect;
    };

    enum class FlyoutKind
    {
        None,
        Children, // a group's children, beside it (closed compact) or below it (Top)
        Overflow, // the top bar's "More" list
    };

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

    // ---- derived state ----
    [[nodiscard]] bool top_mode() const
    {
        return pane_display_mode_ == NavigationShellPaneDisplayMode::Top;
    }
    [[nodiscard]] NavigationShellDisplayMode resolve_display_mode(float width) const;
    /// Whether an open pane overlays the content (Compact or Minimal) and light-dismisses.
    [[nodiscard]] bool overlay_mode() const;
    [[nodiscard]] bool minimal_mode() const;
    [[nodiscard]] bool compact_strip() const; // a closed pane leaves a strip (Compact or Expanded)
    [[nodiscard]] bool closed_compact() const;
    [[nodiscard]] bool light_dismissible() const
    {
        return display_mode_known_ && pane_open_ && overlay_mode() && !top_mode();
    }
    [[nodiscard]] bool  pane_live() const; // any of the pane is showing
    [[nodiscard]] float pane_progress() const;
    [[nodiscard]] bool  show_back_button() const;
    [[nodiscard]] bool  show_close_button() const;
    [[nodiscard]] bool  show_header() const;
    [[nodiscard]] bool  settings_row_shown() const
    {
        return settings_visible_;
    }

    [[nodiscard]] NavEntry*   entry(std::size_t index) const; // null for Settings/detail::NoIndex
    [[nodiscard]] bool        selectable(std::size_t index) const;
    [[nodiscard]] bool        cursor_stop(std::size_t index) const; // an Item row (or Settings)
    [[nodiscard]] bool        is_footer(std::size_t index) const;
    [[nodiscard]] std::size_t parent_of(std::size_t index) const;
    [[nodiscard]] std::size_t depth_of(std::size_t index) const;
    [[nodiscard]] Element*    active_content() const;

    /// Recomputes all_/parent_of_/depth_ and both row lists from entries_/footer_entries_. The
    /// only function allowed to touch them.
    void rebuild_rows();
    void walk(std::vector<NavEntry>& entries, std::size_t depth, std::size_t parent);
    void rows_for(const std::vector<NavEntry>& entries,
                  std::size_t                  first_index,
                  std::size_t                  depth,
                  bool                         inline_children,
                  std::vector<Row>&            out) const;
    void rebuild_visible_rows();

    // ---- selection and invocation ----
    void select(std::size_t index); // no-op on reselect/unselectable, marks dirty, fires the callback
    void invoke(std::size_t index, bool chevron_only, bool from_flyout);
    void move_cursor(int delta);
    void move_flyout_cursor(int delta);
    void open_children_flyout(std::size_t group);
    void open_overflow_flyout();
    void close_flyout();
    void rebuild_flyout_rows();
    void request_pane_open(bool open, bool light_dismiss);
    void update_focus_trap();
    void fire_display_mode(NavigationShellDisplayMode mode);
    void update_adaptive_layout(float width);
    void sync_buttons();

    // ---- layout ----
    /// Arranges `element` at `rect`; an empty rect hides it.
    static void         place(Element* element, const Rect& rect);
    void                layout_left(const Rect& r);
    void                layout_top(const Rect& r);
    void                layout_content(const Rect& grid);
    void                layout_top_items(float left, float right, float y);
    [[nodiscard]] float row_height(const Row& row, bool compact) const;
    [[nodiscard]] float rows_height(const std::vector<Row>& rows, bool compact) const;
    [[nodiscard]] float top_item_width(std::size_t index) const;
    void                clamp_scroll();
    void                scroll_item_into_view(std::size_t index);

    // ---- hit testing ----
    struct Hit
    {
        std::size_t item            = detail::NoIndex; // Settings for Settings, detail::NoIndex for nothing
        bool        chevron         = false;
        bool        overflow_button = false;
    };
    [[nodiscard]] Hit hit_test(Point p) const;

    // ---- render helpers ----
    void render_left_pane(Renderer& renderer, const Theme& theme) const;
    void render_top_bar(Renderer& renderer, const Theme& theme) const;
    void render_content_area(Renderer& renderer, const Theme& theme) const;
    void render_left_rows(Renderer&               renderer,
                          const Theme&            theme,
                          const std::vector<Row>& rows,
                          const Rect&             viewport,
                          float                   scroll,
                          bool                    compact) const;
    void render_top_slot(Renderer& renderer, const Theme& theme, const TopSlot& slot) const;
    void draw_left_indicator(Renderer& renderer, const Theme& theme) const;
    void draw_item_icon(Renderer& renderer, const Theme& theme, const NavEntry* e, const Rect& rect, const Color& color) const;
    void                draw_top_indicator(Renderer& renderer, const Theme& theme) const;
    [[nodiscard]] Color row_text_color(const Theme& theme, std::size_t index, bool hovered, bool pressed) const;

    // ---- model ----
    std::vector<NavEntry>    entries_;
    std::vector<NavEntry>    footer_entries_;
    std::unique_ptr<Element> content_; // the shared content region; see set_content()
    std::unique_ptr<Element> auto_suggest_box_;
    std::unique_ptr<Element> pane_header_;
    std::unique_ptr<Element> pane_custom_content_;
    std::unique_ptr<Element> pane_footer_;

    /// Derived views over entries_ then footer_entries_, all in pre-order: `all_` is index ->
    /// item, `parent_of_` index -> parent index (None at the top level), `depth_` index ->
    /// depth. They hold pointers into the entry vectors, so they are valid until the tree's
    /// *shape* changes. Only set_items()/set_footer_items()/add_item() change it, and all three
    /// rebuild immediately afterwards.
    std::vector<NavEntry*>   all_;
    std::vector<std::size_t> parent_of_;
    std::vector<std::size_t> depth_;
    std::size_t              footer_begin_ = 0;
    bool                     has_groups_   = false;
    /// The rows drawn right now: the menu's (left pane, or the top bar's candidates) and the
    /// footer's, Settings last. Children of a closed-compact or top-bar group are not in them;
    /// they live in the flyout.
    std::vector<Row> menu_rows_;
    std::vector<Row> footer_rows_;

    std::size_t selected_index_ = detail::NoIndex;
    /// The keyboard cursor, as an item index (Settings for Settings). It is not a row, so
    /// collapsing the group it sits in leaves it pointing at something that still exists. It moves over
    /// items and groups alike (otherwise a keyboard could never reach a group to expand it).
    std::size_t cursor_index_ = detail::NoIndex;

    // ---- properties ----
    NavigationShellPaneDisplayMode       pane_display_mode_       = NavigationShellPaneDisplayMode::Auto;
    NavigationShellDisplayMode           display_mode_            = NavigationShellDisplayMode::Minimal;
    AutoVisibility                       back_visible_            = AutoVisibility::Auto;
    NavigationShellSelectionFollowsFocus selection_follows_focus_ = NavigationShellSelectionFollowsFocus::Disabled;
    bool                                 display_mode_known_      = false;
    /// update_adaptive_layout() runs on a width or pane display mode change only. Running it on
    /// every arrange would close a pane opened in Minimal again at once.
    bool  adaptive_pending_ = true;
    float adaptive_width_   = -1.0f;
    bool  pane_open_        = true;
    /// The user closed an Expanded pane with the toggle
    /// button, so growing back into Expanded must not reopen it.
    bool        was_force_closed_   = false;
    bool        pane_visible_       = true;
    bool        toggle_visible_     = true;
    bool        back_enabled_       = false;
    bool        settings_visible_   = true;
    bool        always_show_header_ = true;
    std::string pane_title_;
    std::string header_;
    float       compact_threshold_   = 641.0f;
    float       expanded_threshold_  = 1008.0f;
    float       pane_width_          = 320.0f;
    float       compact_pane_length_ = 48.0f;
    /// A 36px pill plus 2px margin top and bottom.
    float item_row_height_ = 40.0f;

    // ---- owned chrome ----
    // The buttons live inside ToolTips (which own them), so each has its own tooltip.
    std::unique_ptr<ToolTipControl> back_tip_;
    std::unique_ptr<ToolTipControl> close_tip_;
    std::unique_ptr<ToolTipControl> toggle_tip_;
    std::unique_ptr<ButtonControl>  search_button_owned_;
    ButtonControl*                  back_button_    = nullptr;
    ButtonControl*                  close_button_   = nullptr;
    ButtonControl*                  toggle_button_  = nullptr;
    ButtonControl*                  search_button_  = nullptr;
    NavPaneButtonContent*           toggle_content_ = nullptr;
    /// The rows' tooltip: one ToolTip whose empty anchor is moved under the hovered row.
    std::unique_ptr<ToolTipControl> row_tip_;
    // The Settings item's icon: an AnimatedIcon over AnimatedSettingsVisualSource with the
    // Setting symbol as its fallback.
    std::unique_ptr<AnimatedIconControl> settings_icon_;
    std::size_t                          row_tip_item_ = detail::NoIndex;
    /// The children/overflow flyout's list, a popup content element this control owns.
    std::unique_ptr<NavFlyoutList> flyout_list_;
    FlyoutKind                     flyout_kind_  = FlyoutKind::None;
    std::size_t                    flyout_group_ = detail::NoIndex; // the group a Children flyout belongs to
    std::vector<Row>               flyout_rows_;
    std::size_t                    flyout_cursor_      = detail::NoIndex; // item index inside the flyout, keyboard
    bool                           flyout_by_keyboard_ = false;

    // ---- layout results (arrange) ----
    Rect  content_rect_{}; // the header and page together
    Rect  header_rect_{};  // the header's text box
    Rect  page_rect_{};    // the active page
    Rect  pane_rect_{};    // the pane at its full open width (left modes)
    float pane_visible_width_ = 0.0f;
    bool  pane_overlaying_    = false; // drawn over the content: acrylic, frame, shadow
    Rect  menu_viewport_{};
    Rect  footer_viewport_{};
    // The lists' whole areas, where their scroll bars draw: the viewports above, before an era
    // bar's gutter comes off them while the list overflows.
    Rect  menu_bar_rect_{};
    Rect  footer_bar_rect_{};
    Rect  pane_footer_rect_{};
    Rect  separator_rect_{};
    bool  separator_visible_     = false;
    float menu_content_height_   = 0.0f;
    float footer_content_height_ = 0.0f;
    float footer_scroll_offset_  = 0.0f;
    // Top mode.
    Rect                       top_bar_rect_{};
    Rect                       title_rect_{}; // the pane title at the top bar's left
    mutable float              title_width_ = 0.0f;
    std::vector<TopSlot>       top_slots_;      // primary items, overflow button, footer items, Settings
    std::vector<std::size_t>   overflow_items_; // top-level menu items behind "More", in order
    mutable std::vector<float> top_widths_;     // per top-level menu item, from measure
    mutable std::vector<float> footer_top_widths_;
    // Each badge's size from the last measure_override(), by pre-order index.
    mutable std::vector<Size> badge_sizes_;
    mutable float             header_text_height_ = 0.0f;
    mutable Size              asb_desired_{};
    mutable Size              pane_header_desired_{};
    mutable Size              custom_desired_{};
    mutable Size              pane_footer_desired_{};
    // Desired sizes above exclude the focus-ring outset; place() adds it back, so each element's
    // bounds() land exactly on its slot.

    float       pane_scroll_offset_ = 0.0f;
    std::size_t reveal_pending_     = detail::NoIndex; // set_selected_index()'s item, scrolled to by arrange_override()

    // ---- interaction state ----
    std::size_t hovered_item_     = detail::NoIndex; // Settings for Settings
    std::size_t pressed_item_     = detail::NoIndex;
    bool        hovered_chevron_  = false;
    bool        overflow_hovered_ = false;
    bool        overflow_pressed_ = false;

    /// The pane's own overlay scrollbars, for menu and footer items taller than their viewport.
    /// They are not Elements and never join the tree.
    ScrollBarOverlay scrollbar_;
    ScrollBarOverlay footer_scrollbar_;

    // ---- motion ----
    /// The selection indicator's slide: the rect it is traveling from, and how far into the
    /// 600ms clock it is. `indicator_from_` unset means settled. `ticking_` gates every
    /// transition on a host that drives tick_override().
    std::optional<Rect> indicator_from_;
    double              indicator_ms_ = 0.0;
    /// Linear 0..1 openness clock of the pane, and whether the transition running
    /// is an overlay one.
    double pane_clock_         = 1.0;
    bool   overlay_transition_ = false;
    bool   ticking_            = false;

    // Focus trap for an overlaying pane.
    bool         trapping_        = false;
    FocusRequest pending_request_ = FocusRequest::None;

    Callback<void(const std::string&)>                        on_selection_changed_;
    Callback<void(const std::string&)>                        on_item_invoked_;
    Callback<void(const NavigationShellExpandedChangedArgs&)> on_expanded_changed_;
    Callback<void(NavigationShellDisplayMode)>                on_display_mode_changed_;
    Callback<void()>                                          on_pane_opening_;
    Callback<void()>                                          on_pane_opened_;
    Callback<Decision()>                                      on_pane_closing_;
    Callback<void()>                                          on_pane_closed_;
    Callback<void()>                                          on_back_requested_;

protected:
    void enabled_changed() override;
    /// The items' keyboard model: the arrows move the cursor (selecting with
    /// selection_follows_focus()), Enter/Space invoke, Left/Right collapse and expand a group.
    bool key_override(const KeyEvent& e) override;
    /// Gaining focus puts the cursor on the selected item when it has nowhere valid to be.
    void focus_changed(bool focused) override;

protected:
    /// Around the item under the keyboard cursor; none while a flyout of the pane is open.
    [[nodiscard]] Rect focus_ring_rect(const Theme& theme) const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value = this;
    }
    // The selected item's text ("Settings" for Settings), or empty with nothing selected.
    [[nodiscard]] std::string text_value() const override
    {
        return selected_index_ == detail::NoIndex ? std::string{} : text_at(selected_index_);
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

struct NavItem;

/// A built, owned list of navigation entries, the braced-list form of the move-only ui::NavEntry.
/// Callers never name it, since a braced list of `ui::NavItem{...}` converts to it.
class NavList
{
public:
    /// One entry with its content and badge already built; mirrors ui::NavItem's fields.
    struct Entry
    {
        Binding<std::string>      text;
        std::string               key;
        std::unique_ptr<Element>  node{};
        std::vector<Entry>        children{};
        bool                      expanded          = false;
        bool                      selects_on_invoke = true;
        IconSource                icon{};
        std::unique_ptr<Element>  info_badge{};
        NavEntryKind              kind = NavEntryKind::Item;
        std::string               tool_tip{};
        const AnimatedIconSource* animated_icon = nullptr;
    };

    /// No entries.
    NavList() = default;
    /// Builds each item and its nested children in order. Implicit, so
    /// `.items = {ui::NavItem{...}, ...}` works.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, NavItem> && ...))
    NavList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        entries.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    /// Move-only: the entries own their content.
    NavList(const NavList&)            = delete;
    NavList& operator=(const NavList&) = delete;
    NavList(NavList&&)                 = default;
    NavList& operator=(NavList&&)      = default;

    /// The entries, in pane order.
    std::vector<Entry> entries;

private:
    void add(NavItem&& item); // defined after NavItem, which holds a NavList
};

/// One navigation entry: the text in the pane, the content shown while it is selected, and any
/// nested items it groups. A non-empty `children` makes the row a collapsible group with a chevron.
///
/// `.content` may be left unset. The view's own `ui::NavigationShell::content` is then shown for
/// this item.
///
/// Example:
/// @code
/// ui::NavItem{ .text              = "Basic input",
///              .key               = "input",
///              .expanded          = true,
///              .selects_on_invoke = false,
///              .icon              = ui::IconGlyph{0xE73A},
///              .children          = {
///                  ui::NavItem{ .text = "Button", .key = "button", .content = ui::Text{ .text = "Button page" } },
///                  ui::NavItem{ .text = "CheckBox", .key = "checkbox", .content = ui::Text{ .text = "CheckBox page" } },
///              } }
/// @endcode
struct NavItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    // ---- properties ----
    /// The row's text, and its tooltip while the pane is compact. A bound item should have a
    /// `.key`, or its key changes with its text.
    Binding<std::string> text{};
    /// What identifies the item in `.selected_key` and the selection events. Empty uses the
    /// text. "settings" is taken by the Settings item.
    std::string key{};
    /// A group starts with its children showing.
    bool expanded = false;
    /// Whether invoking the row selects it; false makes the row a pure heading.
    bool selects_on_invoke = true;
    /// The row's icon.
    IconSource icon{};
    /// An item, a section header (`text` as its title) or a separator.
    NavEntryKind kind = NavEntryKind::Item;
    /// The row's tooltip; empty shows the text only while the pane is closed compact.
    std::string tool_tip{};
    /// An animated icon over this source (not owned), with `icon` as its fallback; the row plays
    /// its PointerOver/Pressed/Normal transitions.
    const AnimatedIconSource* animated_icon = nullptr;

    // ---- children ----
    /// The page shown while this item is selected; unset falls back to the view's `.content`.
    Spec content{};
    /// A ui::InfoBadge on the row; any other spec is ignored.
    Spec info_badge{};
    /// The items nested under this one.
    NavList children{};
};

inline void NavList::add(NavItem&& item)
{
    entries.push_back(
        Entry{std::move(item.text),
              std::move(item.key),
              item.content.take(),
              std::move(item.children.entries),
              item.expanded,
              item.selects_on_invoke,
              item.icon,
              item.info_badge.take(),
              item.kind,
              std::move(item.tool_tip),
              item.animated_icon});
}

/// A pane of navigation items over swappable content. Builds a ui::NavigationShellControl.
///
/// The selection is a key: an item's `.key`, else its text, and "settings" for the Settings
/// item. Auto display mode picks Expanded, Compact or Minimal from the view's width against the
/// two thresholds.
///
/// Example:
/// @code
/// ui::Property<std::string> page{"mail"};
///
/// window.set_content(ui::NavigationShell{
///     .selected_key         = page, // two-way: a click writes the new key
///     .pane_display_mode    = ui::NavigationShellPaneDisplayMode::Left,
///     .on_selection_changed = [&](const std::string& key) {
///         if (key == ui::NavigationShellControl::SettingsKey)
///             open_settings();
///     },
///     .items                = {
///         ui::NavItem{ .text = "Home", .key = "home", .icon = ui::IconGlyph{0xE80F}, .content = ui::Text{ .text = "Welcome" } },
///         ui::NavItem{ .text = "Mail", .key = "mail", .icon = ui::IconGlyph{0xE715}, .content = ui::Text{ .text = "Inbox" } },
///     },
///     .footer_items = { ui::NavItem{ .text = "Account", .key = "account", .icon = ui::glyphs::Contact } },
/// });
/// @endcode
struct NavigationShell
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<NavigationShell>` points at.
    using control_type = NavigationShellControl;

    // ---- properties ----
    /// The title over the content area; empty draws none.
    Binding<std::string> header{};
    /// Text beside the hamburger button while the pane is open.
    Binding<std::string> pane_title{};
    /// The selected item's key ("settings" for Settings). Empty, or a key no item has, leaves
    /// the first item selected. Bound two-way, the property then takes the key that is selected.
    TwoWayField<std::string> selected_key{};
    /// Whether the pane is open. A Minimal or Compact pane starts closed whatever this says; bind
    /// it to open one later.
    TwoWayField<bool> is_pane_open{true};
    /// The expanded pane's width, in DIPs.
    Binding<float> pane_width{320.0f};
    /// The height of one item row in the left pane, in DIPs (a 36-DIP pill in a 40-DIP slot).
    Binding<float> item_row_height{40.0f};
    /// Where the items go: Left, Top, LeftCompact, LeftMinimal, or Auto (width-driven).
    Binding<NavigationShellPaneDisplayMode> pane_display_mode{NavigationShellPaneDisplayMode::Auto};
    /// False shows the header only in Minimal mode.
    Binding<bool> always_show_header{true};
    /// Shows the built-in Settings item at the bottom of the pane.
    Binding<bool> settings_visible{true};
    /// Whether the back button shows; Auto behaves as Visible.
    Binding<AutoVisibility> back_button_visible{AutoVisibility::Auto};
    /// A visible back button is disabled until this is set (bind it to can_go_back).
    Binding<bool> back_enabled{false};
    /// Shows the hamburger button.
    Binding<bool> pane_toggle_button_visible{true};
    /// False hides the pane (and its toggle button) altogether.
    Binding<bool> pane_visible{true};
    /// Whether moving the keyboard cursor also selects.
    Binding<NavigationShellSelectionFollowsFocus> selection_follows_focus{NavigationShellSelectionFollowsFocus::Disabled};
    /// The closed pane's width in Compact mode, in DIPs.
    Binding<float> compact_pane_length{48.0f};
    /// The width below which Auto is Minimal.
    Binding<float> compact_mode_threshold_width{641.0f};
    /// The width from which Auto is Expanded (Left).
    Binding<float> expanded_mode_threshold_width{1008.0f};

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
    /// Called with the new key whenever the selection moves. A bound value doesn't run it.
    Callback<void(const std::string&)> on_selection_changed{};
    /// Called with the item's key on every click or Enter/Space on an item (Settings included),
    /// selected or not, before the selection moves.
    Callback<void(const std::string&)> on_item_invoked{};
    /// Called when the resolved mode changes, including on the first layout.
    Callback<void(NavigationShellDisplayMode)> on_display_mode_changed{};
    /// Called when the pane is about to open.
    Callback<void()> on_pane_opening{};
    /// Called when the pane has opened.
    Callback<void()> on_pane_opened{};
    /// Called when the pane is about to close; return ui::Cancel to keep it open.
    Callback<Decision()> on_pane_closing{};
    /// Called when the pane has closed.
    Callback<void()> on_pane_closed{};
    /// Raised when the back button is clicked.
    Callback<void()> on_back_requested{};
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
    ControlOut<NavigationShell> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The shared content region, shown for any selected item that carries no `.content` of
    /// its own, typically a ui::NavigationFrame (ui::NavigationShellControl::set_content).
    Spec content{};
    /// The pane's entries, top to bottom.
    NavList items{};
    /// Entries pinned to the bottom of the pane, above Settings.
    NavList footer_items{};
    /// A search box at the top of the pane.
    Spec auto_suggest_box{};
    /// Content beside the pane toggle button.
    Spec pane_header_content{};
    /// Custom pane content: above the menu items, after them in Top mode.
    Spec pane_custom_content{};
    /// Content at the very bottom of the pane.
    Spec pane_footer_content{};

    /// Consumes the spec and returns the built ui::NavigationShellControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

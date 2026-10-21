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
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
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

/// How a Tabs control sizes its tabs.
enum class TabsWidthMode
{
    Equal,         ///< The strip divided evenly, each tab clamped to 100..240 DIPs (the default).
    SizeToContent, ///< Each tab as wide as its icon, header and close button.
    Compact,       ///< Unselected tabs shrink to their icon.
};

/// When a closable tab shows its close button.
enum class TabsCloseButtonOverlayMode
{
    Auto,          ///< the same as Always
    OnPointerOver, ///< on the selected tab and the hovered one
    Always,        ///< on every closable tab
};

/// One tab: a header, an optional icon, and the content shown while it is selected. Written
/// inside the `.items` of a ui::Tabs, or handed to TabsControl::add_tab(). The content is a spec
/// or an already built `std::unique_ptr<Element>`.
///
/// Example:
/// @code
/// auto tabs = std::make_unique<ui::TabsControl>();
/// tabs->add_tab(ui::Tab{ .header = "Document 1", .key = "doc-1", .icon = ui::IconGlyph{0xE8A5},
///                        .content = ui::Text{ .text = "Hello" } });
/// tabs->add_tab(ui::Tab{
///     .header   = "Settings",
///     .key      = "settings",
///     .icon     = ui::IconGlyph{0xE713},
///     .closable = false,
///     .content  = std::make_unique<ui::TextControl>("Settings go here"),
/// });
/// @endcode
struct Tab
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The tab's caption, and its tooltip. A bound header follows its property. A tab without
    /// `.key` keeps the key made from its first header.
    Binding<std::string> header{};
    /// What identifies the tab in `.selected_key` and the close and selection events. Empty
    /// takes the header, numbered " (2)", " (3)", ... after the first tab with it. It stays with
    /// the tab when the tab is dragged elsewhere or renamed.
    std::string key{};
    /// The icon before the header in its 16-DIP box: a Symbol, a glyph or a path in the header's
    /// color, or a picture in its own colors. Empty shows none.
    IconSource icon{};
    /// Shows the close button and accepts middle click and Ctrl+F4.
    bool closable = true;
    /// What the Tabs shows while this tab is selected; owned by the Tabs once added.
    Spec content{};
};

class TabsItemAutomation; // Tabs.cpp: one tab as automation sees it

/// A strip of tabs over the selected tab's content.
///
/// The view owns every tab's content but only measures, arranges and renders the selected
/// tab's content. Each tab has a key (Tab::key, else its header) that the events report and that
/// stays with it when it is dragged elsewhere. The close button, a middle click and Ctrl+F4
/// remove the tab and select its neighbor. With an on_tab_close_requested() handler they raise
/// that instead, and the handler decides, calling remove_tab() to close it. The "+" button shows
/// once there is an on_add_tab_button_click() handler, unless set_add_tab_button_visible() says
/// otherwise. Tabs that do not fit scroll, and dragging a tab reorders the strip. The declarative
/// form is ui::Tabs.
///
/// Example:
/// @code
/// auto  owned = std::make_unique<ui::TabsControl>();
/// auto* tabs  = owned.get();
/// tabs->add_tab("Home", std::make_unique<ui::TextControl>("Start here"));
/// tabs->add_tab(ui::Tab{ .header  = "Document 1",
///                        .key     = "doc-1",
///                        .icon    = ui::IconGlyph{0xE8A5},
///                        .content = std::make_unique<ui::TextControl>("Hello") });
/// tabs->set_closable(0, false);
/// tabs->on_add_tab_button_click([tabs] {
///     tabs->add_tab("New document", std::make_unique<ui::TextControl>("Empty"));
///     tabs->set_selected_index(tabs->tab_count() - 1);
/// });
/// tabs->on_selection_changed([](const std::string& key) { std::println("tab {}", key); });
/// window.set_content(std::move(owned));
/// @endcode
class TabsControl final : public Element, private ValueProvider, private SelectionProvider
{
public:
    /// An empty view: no tabs, and no add button until there is an add handler.
    TabsControl();
    ~TabsControl() override;
    TabsControl(const TabsControl&)            = delete;
    TabsControl& operator=(const TabsControl&) = delete;

    // ---- tabs ----

    /// Appends a tab, taking ownership of its content, and returns a non-owning observer to the
    /// content. The first tab added becomes selected.
    template <typename T>
    T* add_tab(std::string header, std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "Tabs content must be an Element");
        T* observer = content.get();
        insert_tab(tabs_.size(), ui::Tab{.header = std::move(header), .content = std::move(content)});
        return observer;
    }
    /// Appends a tab, building its content; returns the content as an observer (may be null).
    Element* add_tab(ui::Tab tab);
    /// Inserts a tab at `index` (clamped), building its content; returns the content as an
    /// observer (may be null). The selection stays on the tab it was on; the first tab ever added
    /// is selected.
    Element* insert_tab(std::size_t index, ui::Tab tab);
    /// Removes the tab at `index` and destroys its content. Removing the
    /// selected tab selects the one now at its index, or the last.
    void remove_tab(std::size_t index);
    /// Removes the first tab with `key`, as remove_tab(index_of(key)); no-op when there is none.
    void remove_tab(std::string_view key);
    /// Moves a tab to another index, as dragging it does.
    void move_tab(std::size_t from, std::size_t to);

    /// The number of tabs.
    [[nodiscard]] std::size_t tab_count() const
    {
        return tabs_.size();
    }
    /// The header of the tab at `index`; empty when out of range.
    [[nodiscard]] const std::string& header(std::size_t index) const;
    /// The key of the tab at `index`; empty when out of range.
    [[nodiscard]] const std::string& key(std::size_t index) const;
    /// The index of the first tab with `key`; unset when there is none.
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view key) const;
    /// Renames the tab at `index` (ignored out of range) and updates the strip's layout.
    void set_header(std::size_t index, std::string header);
    /// Sets or clears the icon of the tab at `index` and updates the strip's layout.
    void set_icon(std::size_t index, IconSource icon);
    /// Whether the tab at `index` can be closed.
    [[nodiscard]] bool closable(std::size_t index) const;
    /// Shows or hides the tab's close button (and its middle-click and Ctrl+F4 close).
    void set_closable(std::size_t index, bool closable);
    /// The tab's content, or null.
    [[nodiscard]] Element* content(std::size_t index) const;

    // ---- selection ----

    /// The selected tab; unset while there are no tabs.
    [[nodiscard]] std::optional<std::size_t> selected_index() const
    {
        return detail::optional_index(selected_index_);
    }
    /// Selects the tab at `index`, clamped to the tab list. Does nothing, and fires nothing, if
    /// the tab is already selected.
    void set_selected_index(std::size_t index);
    /// The selected tab's key; empty while there are no tabs.
    [[nodiscard]] const std::string& selected_key() const
    {
        return key(selected_index_);
    }
    /// Selects the first tab with `key`. Does nothing, and fires nothing, when no tab has it or
    /// it is already selected.
    void set_selected_key(std::string_view key);
    /// Fired with the new tab's key whenever the selection changes: a click, Enter/Space,
    /// Ctrl+Tab, set_selected_index(), set_selected_key(), or a removal that moves it. The key is
    /// empty once the last tab is gone.
    void on_selection_changed(Callback<void(const std::string&)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- the strip ----

    /// How the tabs share the strip; Equal by default.
    [[nodiscard]] TabsWidthMode tab_width_mode() const
    {
        return width_mode_;
    }
    /// Changes the width mode and updates the layout.
    void set_tab_width_mode(TabsWidthMode mode)
    {
        width_mode_ = mode;
        frozen_tab_width_.reset();
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Which closable tabs show their close button; Auto by default.
    [[nodiscard]] TabsCloseButtonOverlayMode close_button_overlay_mode() const
    {
        return close_mode_;
    }
    /// Changes the overlay mode; redraws.
    void set_close_button_overlay_mode(TabsCloseButtonOverlayMode mode)
    {
        close_mode_ = mode;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Whether the "+" after the last tab shows: the value set, else whether there is an
    /// on_add_tab_button_click() handler.
    [[nodiscard]] bool is_add_tab_button_visible() const
    {
        return add_visible_.value_or(static_cast<bool>(on_add_tab_button_click_));
    }
    /// Shows or hides the add button; `std::nullopt` (the default) shows it while there is an add
    /// handler. Updates the layout.
    void set_add_tab_button_visible(std::optional<bool> visible);
    /// Whether dragging a tab moves it within the strip; true by default.
    [[nodiscard]] bool can_reorder_tabs() const
    {
        return can_reorder_;
    }
    /// Allows or forbids reordering by drag.
    void set_can_reorder_tabs(bool can)
    {
        can_reorder_ = can;
    }
    /// The strip's fill: a fixed color or a theme resource, transparent by default. The content
    /// below it is not filled.
    [[nodiscard]] const Paint& strip_background() const
    {
        return strip_background_;
    }
    /// Sets or clears the strip's fill; redraws.
    void set_strip_background(Paint color)
    {
        strip_background_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The strip header, an element left of the tabs. Takes ownership and returns a non-owning
    /// observer.
    template <typename T>
    T* set_tab_strip_header_content(std::unique_ptr<T> element)
    {
        T* observer = element.get();
        adopt(strip_header_, std::move(element));
        return observer;
    }
    /// The strip footer, an element in the space right of the add button; as
    /// set_tab_strip_header_content().
    template <typename T>
    T* set_tab_strip_footer_content(std::unique_ptr<T> element)
    {
        T* observer = element.get();
        adopt(strip_footer_, std::move(element));
        return observer;
    }

    /// Fired with the tab's key when its close button, a middle click or Ctrl+F4 asks to close
    /// it. With a handler nothing is removed until it calls remove_tab(); without one the tab is
    /// removed and its neighbor selected.
    void on_tab_close_requested(Callback<void(const std::string&)> handler)
    {
        on_tab_close_requested_ = std::move(handler);
    }
    /// Fired when the "+" is clicked. Adding a tab is the handler's job. Setting one shows the
    /// "+" unless set_add_tab_button_visible() set it explicitly.
    void on_add_tab_button_click(Callback<void()> handler);
    /// Fired when a tab is inserted, removed or moved.
    void on_tabs_changed(Callback<void()> handler)
    {
        on_tabs_changed_ = std::move(handler);
    }


    // ---- geometry, from the last arrange ----

    /// The tab strip's rect.
    [[nodiscard]] Rect strip_bounds() const
    {
        return Rect{bounds().x, bounds().y, bounds().width, strip_height_};
    }
    /// The selected tab's content area, below the strip.
    [[nodiscard]] Rect content_bounds() const
    {
        return Rect{bounds().x, bounds().y + strip_height_, bounds().width, (std::max)(0.0f, bounds().height - strip_height_)};
    }
    /// A tab's slot in the strip (scrolled), or nullopt when it is scrolled out of view.
    [[nodiscard]] std::optional<Rect> tab_bounds(std::size_t index) const;
    /// A tab's close button, or nullopt when it shows none.
    [[nodiscard]] std::optional<Rect> close_button_bounds(std::size_t index) const;
    /// The scroll buttons, or nullopt while the tabs fit.
    [[nodiscard]] std::optional<Rect> scroll_decrease_bounds() const;
    [[nodiscard]] std::optional<Rect> scroll_increase_bounds() const;
    /// How far the strip is scrolled, in DIPs (0 is the first tab at the left).
    [[nodiscard]] float scroll_offset() const
    {
        return scroll_offset_;
    }
    /// The "+" button (owned by the view).
    [[nodiscard]] ButtonControl* add_button() const
    {
        return add_button_;
    }
    /// Ctrl+Tab, Ctrl+Shift+Tab and Ctrl+F4 anywhere inside the view, then any shortcuts
    /// added with add_shortcut().
    bool handle_shortcut(const KeyEvent& e) override;

    // ---- focus ----
    /// Whether the strip has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The strip is one tab stop while enabled and not empty.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && !tabs_.empty();
    }
    /// Only a press on a tab focuses the strip. The selected content takes focus on its own.
    [[nodiscard]] bool takes_focus_at(Point p) const override;
    /// The tabs (this), the strip header, the add button, the strip footer, then the selected
    /// tab's content.
    void collect_focusables(std::vector<Element*>& out) override;

    // ---- children ----
    /// Lists the tooltips, the strip header and footer, and the selected tab's content. An
    /// unselected tab's content is set aside, and nothing reaches it until it is selected again.
    void for_each_child(ChildVisitor visit) const override;
    /// Passes native-control hosting on to the strip parts and the selected tab's content.
    void sync_native(NativeHost& host) override;

    // ---- automation ----
    /// The strip header, one tab item per tab, the add button, the strip footer, then the
    /// selected tab's content. The control also exposes the selected header as a read-only value.
    void automation_children(std::vector<Element*>& out) const override;
    /// The tab the keyboard is on, so a screen reader reads it as the arrow keys move.
    [[nodiscard]] Element* automation_focus() const override;
    /// A Tab control.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Tab;
    }

protected:
    // ---- layout ----
    /// Fills `available` on whichever axes are bounded.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Lays out the strip (tab widths, scroll buttons) and the selected tab's content below it.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- render ----
    /// Draws the strip and its tabs, then the selected tab's content.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- input ----
    /// Clicks select and close tabs, a middle click asks to close, and a drag reorders. Other
    /// pointer events reach the strip header/footer and the selected content.
    bool pointer_override(const PointerEvent& e) override;
    /// A turn over an overflowing strip scrolls the strip. Otherwise the turn reaches the tab's
    /// content through handle_wheel(), so a scrollable inside the tab wins over one wrapping the
    /// whole Tabs.
    bool wheel_override(const PointerEvent& e) override;
    void hit_test_children(Point p, std::vector<Element*>& out) const override;
    /// Repeats a held scroll button.
    void tick_override(Duration delta) override;


private:
    // Enter selects the focused tab.
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // Left/Right move between tabs; claimed so Window's directional focus navigation leaves
    // them alone (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    // The strip's tooltips themselves (not what is inside them), then the strip parts' and the
    // selected content's live elements, without this control.
    void collect_live(std::vector<Element*>& out) override;
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

    friend class TabsItemAutomation;
    friend class TabsCloseAutomation;

    // The TabItem node of the tab at `index`, made on first use. One per place in the strip, kept
    // for the control's life, so a client holding one never sees it freed.
    [[nodiscard]] TabsItemAutomation* item_automation(std::size_t index) const;
    void                              place_item_automation();

    void               select(std::size_t index); // no-op on reselect, marks dirty, fires the callback
    void               request_close(std::size_t index);
    [[nodiscard]] bool close_button_shown(std::size_t index) const;
    [[nodiscard]] bool compact_tab(std::size_t index) const
    {
        return width_mode_ == TabsWidthMode::Compact && index != selected_index_;
    }
    /// A tab's natural width (its SizeToContent width): its border, padding, icon, header and
    /// close button.
    [[nodiscard]] float       natural_width(std::size_t index) const;
    [[nodiscard]] float       tab_x(std::size_t index) const; // unscrolled, from the list's left
    [[nodiscard]] std::size_t tab_at(Point p) const;
    [[nodiscard]] Rect        viewport() const
    {
        return viewport_;
    }
    void                           set_scroll_offset(float offset);
    [[nodiscard]] std::string      unique_key(const std::string& header) const; // a keyless tab's key
    void                           bring_into_view(std::size_t index);
    [[nodiscard]] static TextStyle header_style(const Theme& theme, bool selected);
    void render_tab(Renderer& renderer, const Theme& theme, std::size_t index, const Rect& slot) const;

    struct Tab
    {
        std::string              header;
        std::string              key;
        std::unique_ptr<Element> content;
        IconSource               icon;
        bool                     closable = true;
        // From measure(): the header's width in its unselected and selected weights.
        mutable float text_width          = 0.0f;
        mutable float text_width_selected = 0.0f;
        // From arrange(): the tab's width in the strip.
        float width = 0.0f;
    };

    std::vector<Tab>                tabs_;
    std::size_t                     selected_index_ = detail::NoIndex;
    TabsWidthMode                   width_mode_     = TabsWidthMode::Equal;
    TabsCloseButtonOverlayMode      close_mode_     = TabsCloseButtonOverlayMode::Auto;
    std::optional<bool>             add_visible_; // unset: shown while there is an add handler
    bool                            can_reorder_ = true;
    Paint                           strip_background_;
    std::unique_ptr<Element>        strip_header_;
    std::unique_ptr<Element>        strip_footer_;
    std::unique_ptr<ToolTipControl> add_tip_;
    ButtonControl*                  add_button_ = nullptr;
    /// The strip's tooltip (a tab's header, a close button's "Close tab (Ctrl+F4)", a scroll
    /// button's name): one ToolTip whose empty anchor moves under what the pointer is over.
    std::unique_ptr<ToolTipControl> hover_tip_;
    int                             hover_tip_key_ = -1;

    // ---- layout results ----
    mutable float        item_height_  = 32.0f;
    mutable float        strip_height_ = 40.0f;
    mutable Size         header_desired_{};
    mutable Size         footer_desired_{};
    mutable float        page_inset_ = 0.0f;     // measure(): an era page's border and padding round the content
    Rect                 viewport_{};            // where the tabs are drawn (between the scroll buttons)
    bool                 overflow_      = false; // the scroll buttons show
    float                list_width_    = 0.0f;  // header (4) + tabs + footer (4)
    float                scroll_offset_ = 0.0f;
    std::optional<float> frozen_tab_width_; // Equal width held while the pointer is in the strip
    bool                 pointer_in_strip_ = false;

    // ---- interaction ----
    std::size_t hovered_index_    = detail::NoIndex;
    std::size_t pressed_index_    = detail::NoIndex;
    std::size_t close_hovered_    = detail::NoIndex;
    std::size_t close_pressed_    = detail::NoIndex;
    std::size_t cursor_index_     = detail::NoIndex; // the keyboard's focused tab
    int         scroll_hovered_   = 0;               // -1 decrease, +1 increase
    int         scroll_pressed_   = 0;
    double      scroll_repeat_ms_ = 0.0; // held-button repeat: 50 ms delay, then every 100 ms
    bool        dragging_         = false;
    Point       press_point_{};

    Callback<void(const std::string&)> on_selection_changed_;
    Callback<void(const std::string&)> on_tab_close_requested_;
    Callback<void()>                   on_add_tab_button_click_;
    Callback<void()>                   on_tabs_changed_;

    mutable std::vector<std::unique_ptr<TabsItemAutomation>> item_automation_;

protected:
    void enabled_changed() override;
    /// Left/Right move the keyboard cursor between tabs; Enter/Space select it.
    bool key_override(const KeyEvent& e) override;
    /// Called by the Window as focus arrives or leaves; redraws the focused tab.
    void focus_changed(bool focused) override;

protected:
    /// Inside the focused tab, cut to the part of it the strip shows.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override
    {
        return theme.overlay_corner_radius;
    }
    [[nodiscard]] bool focus_ring_inside() const override
    {
        return true;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value     = this;
        out.selection = this;
    }
    // The selected tab's TabItem node.
    void               selected_items(std::vector<Element*>& out) const override;
    [[nodiscard]] bool can_select_multiple() const override
    {
        return false;
    }
    [[nodiscard]] bool selection_required() const override
    {
        return !tabs_.empty();
    }
    // The selected tab's header, or empty with no tabs.
    [[nodiscard]] std::string text_value() const override
    {
        return selected_index_ < tabs_.size() ? tabs_[selected_index_].header : std::string{};
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

/// A built, owned list of tabs. A braced list of `ui::Tab{...}` converts to it, building each
/// tab's content as it goes.
///
/// Example:
/// @code
/// ui::TabList tabs = { ui::Tab{ .header = "One", .content = ui::Text{ .text = "1" } },
///                      ui::Tab{ .header = "Two", .content = ui::Text{ .text = "2" } } };
/// window.set_content(ui::Tabs{ .items = std::move(tabs) });
/// @endcode
class TabList
{
public:
    /// One tab with its content already built.
    struct Entry
    {
        Binding<std::string>     header;
        std::unique_ptr<Element> node{};
        IconSource               icon{};
        bool                     closable = true;
        std::string              key{};
    };

    /// No tabs.
    TabList() = default;
    /// Builds each tab's content in order. Implicit, so `.items = {ui::Tab{...}, ...}` works.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, Tab> && ...))
    TabList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        entries.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    /// Move-only: the entries own their content.
    TabList(const TabList&)            = delete;
    TabList& operator=(const TabList&) = delete;
    TabList(TabList&&)                 = default;
    TabList& operator=(TabList&&)      = default;

    /// The tabs, in strip order.
    std::vector<Entry> entries;

    /// Appends one tab, building its content, for a list put together in a loop.
    void add(Tab tab)
    {
        entries.push_back(Entry{std::move(tab.header), tab.content.take(), tab.icon, tab.closable, std::move(tab.key)});
    }
};

/// A tab strip over swappable content. Builds a ui::TabsControl. The first tab is selected
/// unless `.selected_key` names another. A tab's close button removes it, unless
/// `.on_tab_close_requested` is set. Then the handler decides, and closes the tab by calling
/// remove_tab() through `.control`. The "+" shows once `.on_add_tab_button_click` is set.
///
/// Example:
/// @code
/// ui::Control<ui::Tabs>     tabs;
/// ui::Property<std::string> current;
/// int                       documents = 1;
///
/// window.set_content(ui::Tabs{
///     .selected_key            = current, // "home", then whichever tab the user picks
///     .tab_width_mode          = ui::TabsWidthMode::SizeToContent,
///     .on_tab_close_requested  = [&](const std::string& key) { tabs->remove_tab(key); },
///     .on_add_tab_button_click = [&] {
///         tabs->add_tab(ui::Tab{.header = "New document", .key = std::format("doc-{}", ++documents)});
///         tabs->set_selected_index(tabs->tab_count() - 1);
///     },
///     .control = tabs,
///     .items   = {
///         ui::Tab{ .header = "Home", .key = "home", .closable = false, .content = ui::Text{ .text = "Start here" } },
///         ui::Tab{ .header = "Document 1", .key = "doc-1", .content = ui::Text{ .text = "Hello" } },
///     },
/// });
/// @endcode
struct Tabs
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Tabs>` points at.
    using control_type = TabsControl;

    // ---- properties ----
    /// The selected tab's key. Empty, or a key no tab has, leaves the first tab selected. Bound
    /// two-way, the property then takes the key of the tab that is selected.
    TwoWayField<std::string> selected_key{};
    /// How the tabs share the strip: Equal (clamped to 100..240 DIPs), SizeToContent, or Compact.
    Binding<TabsWidthMode> tab_width_mode{TabsWidthMode::Equal};
    /// Which closable tabs show their close button.
    Binding<TabsCloseButtonOverlayMode> close_button_overlay_mode{TabsCloseButtonOverlayMode::Auto};
    /// Shows the "+" after the last tab. Unset shows it when `.on_add_tab_button_click` is set.
    Binding<std::optional<bool>> add_tab_button_visible{};
    /// Dragging a tab moves it within the strip.
    Binding<bool> can_reorder_tabs{true};
    /// The strip's fill: a fixed color or a theme resource; unset is transparent.
    Binding<Paint> strip_background{};

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
    /// Called with the new tab's key whenever the selection moves. A bound value doesn't run it.
    Callback<void(const std::string&)> on_selection_changed{};
    /// Called with the tab's key on its close button, a middle click or Ctrl+F4. With a handler,
    /// nothing is removed until it calls ui::TabsControl::remove_tab(). Without one, the tab
    /// closes.
    Callback<void(const std::string&)> on_tab_close_requested{};
    /// Called when the "+" is clicked; the handler adds the tab. Setting it shows the "+".
    Callback<void()> on_add_tab_button_click{};
    /// Called when a tab is inserted, removed or moved.
    Callback<void()> on_tabs_changed{};
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
    ControlOut<Tabs> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The initial tabs, in strip order.
    TabList items{};
    /// An element left of the tabs.
    Spec tab_strip_header_content{};
    /// An element in the space right of the add button.
    Spec tab_strip_footer_content{};

    /// Consumes the spec and returns the built ui::TabsControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

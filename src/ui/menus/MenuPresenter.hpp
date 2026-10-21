// SPDX-License-Identifier: MIT
#pragma once

#include "../themes/ControlChrome.hpp"
#include "winui/core/Index.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"

#include <memory>
#include <optional>
#include <vector>

#include <cstddef>

namespace ui::detail
{

/// What a root presenter reports to the control that owns the menu.
class MenuHost
{
public:
    // Public and virtual: an owner deletes its host through a unique_ptr, so the host's
    // destructor is public, and a public destructor beside virtual functions is a virtual one.
    virtual ~MenuHost() = default;

    /// An item was clicked, or chosen with the keyboard: close everything and run it
    /// (invoke_menu_item()). Called last by the presenter, which touches nothing after it.
    virtual void menu_invoke(MenuItem& item) = 0;
    /// Whether the item holding focus shows the keyboard focus visual.
    [[nodiscard]] virtual bool menu_keyboard_visual() const = 0;
};

class MenuItemAutomation; // MenuPresenter.cpp: one item as automation sees it

class MenuPresenter final : public Element
{
public:
    MenuPresenter(MenuHost& host, MenuPresenter* parent);
    ~MenuPresenter() override;
    MenuPresenter(const MenuPresenter&)            = delete;
    MenuPresenter& operator=(const MenuPresenter&) = delete;

    /// The level's items; closes any submenu. Null empties the presenter.
    void                                 set_items(std::vector<MenuItem>* items);
    [[nodiscard]] std::vector<MenuItem>* items() const
    {
        return items_;
    }

    /// Starts showing this level: the first focusable item takes focus (shown only once the
    /// keyboard is used, or at once when `from_keyboard`) and the open slide starts.
    void open(bool from_keyboard);
    /// Starts the 83 ms close fade (a submenu's; a root's fade is its owner's).
    void begin_close();
    /// Closed at once, submenus too.
    void               close_now();
    [[nodiscard]] bool is_open() const
    {
        return phase_ == Phase::Opening || phase_ == Phase::Open;
    }
    [[nodiscard]] bool visible() const
    {
        return phase_ != Phase::Closed;
    }
    /// This level's own opacity: whole while opening (fading in where the theme's menus fade),
    /// fading while closing.
    [[nodiscard]] float self_opacity() const;
    /// The theme's `popup_motion`, as measure_override()/render_override() last saw it — for the owners'
    /// own clocks, which run in tick_override() without a Theme.
    [[nodiscard]] const Theme::PopupMotion& motion() const
    {
        return motion_;
    }
    /// Whether the open slide comes up from below (the menu opened above what it hangs from)
    /// rather than down from above. The owner says so for a root; a presenter works it out for
    /// the submenus it places.
    void set_slides_up(bool up)
    {
        slides_up_ = up;
    }
    /// For a root: the height of what the menu hangs from (its anchor's center, or the point it
    /// was shown at). Once placed, a menu above it slides up and one below it slides down,
    /// whichever way the popup layer flipped it.
    void set_slide_anchor(std::optional<float> y)
    {
        slide_anchor_ = y;
    }

    [[nodiscard]] std::size_t focused_index() const
    {
        return focused_;
    }
    [[nodiscard]] std::size_t open_child_index() const
    {
        return child_index_;
    }
    [[nodiscard]] MenuPresenter* open_child() const;

    /// Opens the submenu of item `index` (a SubItem or Split item), closing any other at once.
    void open_child(std::size_t index, bool from_keyboard);
    /// Closes the open submenu: faded, or at once.
    void close_child(bool animate);

    /// The keys of the whole cascade, taken by the root and acted on by the deepest open level.
    /// False when the key means nothing there (Left or Right at the root, which a MenuBar uses to
    /// move between its menus).
    bool route_key(const KeyEvent& e);

    /// Whether the root takes keyboard focus itself (a MenuFlyout's does; a MenuBar keeps focus
    /// on its header and passes the keys on).
    void set_takes_focus(bool takes_focus)
    {
        takes_focus_ = takes_focus;
    }
    [[nodiscard]] bool focusable() const override
    {
        return takes_focus_ && items_ != nullptr;
    }
    [[nodiscard]] bool takes_arrow_key(Key /*key*/) const
    {
        return true;
    }
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }

    // ---- the submenu this level floats ----
    [[nodiscard]] float transition_opacity() const override;

    // ---- automation ----
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Menu;
    }
    /// One MenuItem node per item (a Separator node for a separator).
    void automation_children(std::vector<Element*>& out) const override;
    /// An open submenu belongs to the item it drops from.
    [[nodiscard]] Element* automation_parent_of(const Element& child) const override;
    /// The item the keyboard is on in the deepest open level; nullptr when none is.
    [[nodiscard]] Element* automation_focus() const override;

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        if (slide_anchor_)
        {
            slides_up_ = final_rect.center().y < *slide_anchor_;
        }
        place_item_automation();
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    bool pointer_override(const PointerEvent& e) override;
    bool wheel_override(const PointerEvent& e) override;
    void tick_override(Duration delta) override;
    void clear_dirty_override(DirtyFlags flags) override;


private:
    friend class MenuItemAutomation;

    // The node of item `index`, made on first use and kept while this level lives.
    [[nodiscard]] MenuItemAutomation* item_automation(std::size_t index) const;
    void                              place_item_automation();

    mutable std::vector<std::unique_ptr<MenuItemAutomation>> item_automation_;

    // Opens, updates or closes the popup this level floats its open submenu in, after the
    // submenu's phase or the open item changed. A submenu already fading out is past dismissing:
    // the next Escape goes to the menu around it.
    void sync_popup();
    // This level's phase changed: the level around it floats it, so that one re-syncs its popup.
    void phase_changed()
    {
        if (parent_ != nullptr)
        {
            parent_->sync_popup();
        }
    }
    // A submenu's placement: 4 DIPs over the item's right edge and level with it,
    // flipped left (or pinned to the right edge) and up (or pinned to the top) where it does
    // not fit.
    bool place_submenu(Element& content, Size client, const MeasureContext& context);

    enum class Phase
    {
        Closed,
        Opening,
        Open,
        Closing,
    };
    enum class Part
    {
        Whole,     // the item, or a Split item's primary part
        Secondary, // a Split item's chevron button
    };

    struct Metrics
    {
        float row_height     = 32.0f; // an item's slot, margins included
        float check_column   = 0.0f;  // 28 when any item is a toggle or radio
        float icon_column    = 0.0f;  // 28 when any item has an icon
        float shortcut_width = 0.0f;
        bool  any_submenu    = false;
    };

    MenuHost&              host_;
    MenuPresenter*         parent_ = nullptr;
    std::vector<MenuItem>* items_  = nullptr;
    // One per item index, kept while this level lives: Window's popup list, rebuilt only on
    // layout, may still point at a closed one.
    std::vector<std::unique_ptr<MenuPresenter>> pool_;
    std::size_t                                 child_index_ = detail::NoIndex;

    Phase                phase_     = Phase::Closed;
    double               phase_ms_  = 0.0;
    bool                 slides_up_ = false;
    std::optional<float> slide_anchor_{};

    std::size_t focused_      = detail::NoIndex; // logical focus (keyboard, or the item last pointed at)
    std::size_t hovered_      = detail::NoIndex;
    Part        hovered_part_ = Part::Whole;
    std::size_t pressed_      = detail::NoIndex;
    Part        pressed_part_ = Part::Whole;
    bool        key_pressed_  = false; // Enter/Space held on focused_
    bool        pointer_mode_ = false; // the pointer moved since the keyboard was last used
    bool        focused_self_ = false;
    bool        takes_focus_  = false;

    // The hover delays: a submenu opens once the pointer has rested on its item,
    // and one no longer pointed at closes the same time later.
    std::size_t pending_open_  = detail::NoIndex;
    double      open_wait_ms_  = 0.0;
    bool        pending_close_ = false;
    double      close_wait_ms_ = 0.0;

    float scroll_ = 0.0f; // how far the items are scrolled up when the menu is taller than its room

    mutable Metrics            metrics_{};
    mutable float              border_ = 1.0f;
    mutable Theme::PopupMotion motion_{};
    mutable float              content_height_ = 0.0f; // items only, without padding and border
    // measure(): the theme's layout, which hit testing needs between frames. An era menu
    // (ControlChrome::era_menu) has its own padding, no item margin and a taller separator.
    mutable bool      era_ = false;
    mutable Thickness padding_{};
    mutable Thickness item_margin_{};
    mutable float     separator_slot_ = 0.0f;

    [[nodiscard]] std::size_t count() const
    {
        return items_ ? items_->size() : 0;
    }
    [[nodiscard]] float row_height(std::size_t index) const;
    // An item's (or a Split item's part's) look: 0 at rest, 1 hot, 2 pressed.
    [[nodiscard]] int item_state(std::size_t index, Part part) const;
    // The era menu's body, between the slide and the border: gutter, rows and separators.
    void               render_era(Renderer& renderer, const Theme& theme, const ControlChrome::EraMenu& menu) const;
    [[nodiscard]] Rect slot_rect(std::size_t index) const; // full width, margins included
    [[nodiscard]] Rect item_rect(std::size_t index) const; // the drawn item, inside its margin
    [[nodiscard]] Rect split_button_rect(std::size_t index) const;
    [[nodiscard]] Rect viewport() const; // inside the border and padding
    [[nodiscard]] std::size_t    hit_test(Point p, Part* part = nullptr) const;
    [[nodiscard]] bool           keyboard_visual() const;
    [[nodiscard]] MenuPresenter* root();
    [[nodiscard]] MenuPresenter* deepest_open();

    void move_focus(int step);
    void focus_first();
    void scroll_into_view(std::size_t index);
    void activate(std::size_t index, Part part, bool from_keyboard);
    bool handle_level_key(const KeyEvent& e);
    void child_pointer_entered(); // a submenu took the pointer: keep it open

protected:
    void focus_changed(bool focused) override;
    bool key_override(const KeyEvent& e) override;

protected:
    /// Rings the keyboard's item itself: the menu shows it while the bar or flyout that opened it
    /// has focus, so the window's ring pass never sees it.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }
};

} // namespace ui::detail

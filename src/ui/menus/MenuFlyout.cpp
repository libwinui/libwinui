// SPDX-License-Identifier: MIT
#include "winui/ui/menus/MenuFlyout.hpp"

#include "../dialogs/FlyoutSpec.hpp"
#include "MenuInvoke.hpp"
#include "MenuPresenter.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

namespace
{
template <typename F>
void visit_items(std::vector<MenuItem>& items, F&& f)
{
    for (MenuItem& item : items)
    {
        f(item);
        visit_items(item.items, f);
    }
}

// Sets the state an item shows, keeping the writer of a two-way `checked` and its group.
void set_menu_check(MenuItem& item, bool checked)
{
    const void* group = item.checked.selection_group();
    item.checked      = Binding<bool>{checked, nullptr, item.checked.take_writer(), group};
}

// Follows one bound field of `item`: the field becomes a constant holding the property's value,
// replaced on every change, so a copy of the items never subscribes again.
template <typename T>
void follow_field(Binding<T>& field, detail::MenuChecks& checks)
{
    if (!field.reactive())
    {
        return;
    }
    T now = field.current();
    checks.subscriptions.push_back(field.subscribe(
        [&checks, &field](T value)
        {
            field = Binding<T>{std::move(value)};
            if (checks.changed)
            {
                checks.changed();
            }
        }));
    field = Binding<T>{std::move(now)};
}

// The list `item` sits in, within `items` and its submenus; null when it is in none of them.
std::vector<MenuItem>* list_of(std::vector<MenuItem>& items, const MenuItem& item)
{
    for (MenuItem& candidate : items)
    {
        if (&candidate == &item)
        {
            return &items;
        }
        if (std::vector<MenuItem>* found = list_of(candidate.items, item))
        {
            return found;
        }
    }
    return nullptr;
}

// The writer of an item's two-way `checked`, copied; empty when it is not two-way.
Binding<bool>::Write menu_check_writer(const MenuItem& item)
{
    Binding<bool> copy = item.checked;
    return copy.take_writer();
}

MenuItem* find_shortcut_in(std::vector<MenuItem>& items, const KeyEvent& e)
{
    for (MenuItem& item : items)
    {
        if (!item.is_enabled() || item.kind == MenuItemKind::Separator)
        {
            continue;
        }
        if (item.kind != MenuItemKind::SubItem && item.display_shortcut().matches(e))
        {
            return &item;
        }
        if (MenuItem* found = find_shortcut_in(item.items, e))
        {
            return found;
        }
    }
    return nullptr;
}
} // namespace

void follow_menu_bindings(std::vector<MenuItem>& items, detail::MenuChecks& checks)
{
    visit_items(items,
                [&checks](MenuItem& item)
                {
                    follow_field(item.text, checks);
                    follow_field(item.icon, checks);
                    follow_field(item.shortcut, checks);
                    follow_field(item.enabled, checks);
                    if (!item.checked.reactive())
                    {
                        return;
                    }
                    const bool now = item.checked.current();
                    checks.subscriptions.push_back(item.checked.subscribe(
                        [&checks, &item](bool checked)
                        {
                            if (item.is_checked() == checked)
                            {
                                return;
                            }
                            set_menu_check(item, checked);
                            if (checks.changed)
                            {
                                checks.changed();
                            }
                        }));
                    set_menu_check(item, now); // drops the subscribe thunk, keeps the writer
                });
}

void invoke_menu_item(std::vector<MenuItem>& root, MenuItem& item, const std::function<void()>& before_run)
{
    if (!item.is_enabled())
    {
        return;
    }
    // A toggle item flips `checked` before on_click runs. A radio item checks itself (a checked
    // one stays checked) and unchecks the rest of its group.
    struct CheckChange
    {
        Binding<bool>::Write      write;
        std::function<void(bool)> handler;
        bool                      checked = false;
    };
    std::vector<CheckChange> changes;
    const auto               change = [&changes](MenuItem& target, bool checked)
    {
        set_menu_check(target, checked);
        changes.push_back({menu_check_writer(target), target.on_checked_changed, checked});
    };
    if (item.kind == MenuItemKind::Toggle)
    {
        change(item, !item.is_checked());
    }
    else if (item.kind == MenuItemKind::Radio && !item.is_checked())
    {
        if (const void* group = item.checked.selection_group(); group != nullptr)
        {
            // Bound with ui::selects(): every radio item bound to the same property, anywhere.
            visit_items(root,
                        [&](MenuItem& other)
                        {
                            if (&other != &item && other.kind == MenuItemKind::Radio && other.is_checked() &&
                                other.checked.selection_group() == group)
                            {
                                change(other, false);
                            }
                        });
        }
        else if (std::vector<MenuItem>* list = list_of(root, item))
        {
            // Unbound: the run of radio items next to each other around this one.
            const auto at = static_cast<std::size_t>(&item - list->data());
            for (std::size_t i = at; i-- > 0 && (*list)[i].kind == MenuItemKind::Radio;)
            {
                if ((*list)[i].checked.selection_group() == nullptr && (*list)[i].is_checked())
                {
                    change((*list)[i], false);
                }
            }
            for (std::size_t i = at + 1; i < list->size() && (*list)[i].kind == MenuItemKind::Radio; ++i)
            {
                if ((*list)[i].checked.selection_group() == nullptr && (*list)[i].is_checked())
                {
                    change((*list)[i], false);
                }
            }
        }
        change(item, true);
    }
    // Copied out before anything runs: a handler may replace the items `item` lives in.
    const std::function<void()> click     = item.on_click;
    const Command* const        command   = item.command;
    const std::string           parameter = item.command_parameter;

    if (before_run)
    {
        before_run();
    }
    // The properties first, so a handler sees every one of them written.
    for (CheckChange& c : changes)
    {
        if (c.write)
        {
            c.write(c.checked);
        }
    }
    for (CheckChange& c : changes)
    {
        if (c.handler)
        {
            c.handler(c.checked);
        }
    }
    if (click)
    {
        click();
    }
    if (command != nullptr)
    {
        (*command)(parameter);
    }
}

MenuItem* find_menu_shortcut(std::vector<MenuItem>& root, const KeyEvent& e)
{
    return find_shortcut_in(root, e);
}

// ---- MenuFlyout ----

class MenuFlyoutControl::Host final : public detail::MenuHost
{
public:
    explicit Host(MenuFlyoutControl& owner) : owner_(owner)
    {
    }

    void menu_invoke(MenuItem& item) override
    {
        MenuFlyoutControl& owner = owner_;
        invoke_menu_item(owner.items_, item, [&owner] { owner.close(PopupCloseReason::Invoked); });
    }

    [[nodiscard]] bool menu_keyboard_visual() const override
    {
        return owner_.presenter_ && owner_.presenter_->focus_visible();
    }

private:
    MenuFlyoutControl& owner_;
};

MenuFlyoutControl::MenuFlyoutControl() :
    host_(std::make_unique<Host>(*this)),
    checks_(std::make_unique<detail::MenuChecks>())
{
    checks_->changed = [this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); };
    // A 250 ms slide in and an 83 ms fade out, until the presenter has seen the theme.
    set_transition_durations(Duration{250.0}, Duration{83.0});
}

void MenuFlyoutControl::tick_override(Duration delta)
{
    // Only the presenter sees the Theme, so the era's menu timing comes from it.
    if (presenter_)
    {
        const Theme::PopupMotion& motion = presenter_->motion();
        set_transition_durations(motion.open, motion.close);
    }
    FlyoutBase::tick_override(delta);
}

MenuFlyoutControl::~MenuFlyoutControl() = default;

void MenuFlyoutControl::set_items(std::vector<MenuItem> items)
{
    if (presenter_)
    {
        presenter_->close_now();
        presenter_->set_items(nullptr);
    }
    if (is_open())
    {
        hide();
    }
    checks_->subscriptions.clear(); // they point into the items about to go
    items_ = std::move(items);
    follow_menu_bindings(items_, *checks_);
    if (presenter_)
    {
        presenter_->set_items(&items_);
    }
    watch_commands();
}

void MenuFlyoutControl::watch_commands()
{
    // Items read their command whenever they are drawn or invoked, so a change only needs a new
    // layout (a label may be longer) and a repaint.
    command_changed_.clear();
    visit_items(items_,
                [this](MenuItem& item)
                {
                    if (item.command == nullptr)
                    {
                        return;
                    }
                    command_changed_.push_back(
                        item.command->observe([this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); }));
                });
}

Element* MenuFlyoutControl::flyout_surface()
{
    if (!presenter_)
    {
        presenter_ = std::make_unique<detail::MenuPresenter>(*host_, nullptr);
        set_parent_of(*presenter_, this);
        presenter_->set_takes_focus(true);
        presenter_->set_items(&items_);
    }
    return presenter_.get();
}

void MenuFlyoutControl::opening_override()
{
    (void)flyout_surface();
    presenter_->set_items(&items_);
    presenter_->open(false);
    // A menu above its target slides up, one below it slides down. Decided once the popup layer has placed it.
    const Element* target = anchor() != nullptr ? anchor() : this;
    presenter_->set_slide_anchor(target->bounds().center().y);
}

void MenuFlyoutControl::closing_override()
{
    if (presenter_)
    {
        presenter_->close_child(false); // the submenus close first
    }
}

float MenuFlyoutControl::transition_opacity() const
{
    switch (phase())
    {
        case FlyoutPhase::Closing:
            return 1.0f - transition_progress();
        case FlyoutPhase::Closed:
            return 0.0f;
        case FlyoutPhase::Opening:
            return presenter_ ? presenter_->self_opacity() : 1.0f;
        case FlyoutPhase::Open:
            break;
    }
    return 1.0f;
}

bool MenuFlyoutControl::handle_global_shortcut(const KeyEvent& e)
{
    if (FlyoutBase::handle_global_shortcut(e))
    {
        return true;
    }
    MenuItem* item = find_menu_shortcut(items_, e);
    if (item == nullptr)
    {
        return false;
    }
    invoke_menu_item(items_,
                     *item,
                     [this]
                     {
                         if (is_open())
                         {
                             close(PopupCloseReason::Invoked);
                         }
                     });
    return true;
}

std::unique_ptr<ui::Element> MenuFlyout::build() &&
{
    auto node = std::make_unique<ui::MenuFlyoutControl>();
    apply(items, *node, [raw = node.get()](const std::vector<MenuItem>& v) { raw->set_items(v); });
    apply(placement, *node, [raw = node.get()](const PopupPlacement& v) { raw->set_placement(v); });
    apply(placement_alignment,
          *node,
          [raw = node.get()](const PopupPlacementAlignment& v) { raw->set_placement_alignment(v); });
    if (anchor)
    {
        node->set_anchor(anchor.take());
    }
    detail::apply_flyout_open(*node, is_open, light_dismiss, on_opened, on_closing, on_closed);

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

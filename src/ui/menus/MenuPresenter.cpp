// SPDX-License-Identifier: MIT
#include "MenuPresenter.hpp"

#include "../../render/Compositor.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <cstddef>

namespace ui::detail
{

namespace
{
// MenuFlyout metrics.
constexpr Thickness PresenterPadding        = Symmetric{.horizontal = 0.0f, .vertical = 2.0f}; // the menu's padding
constexpr float     MenuPresenterMinWidth   = 96.0f; // the menu's minimum width
constexpr float     MenuPresenterMinHeight  = 32.0f; // the menu's minimum height
constexpr Thickness MenuPresenterItemMargin = Symmetric{.horizontal = 4.0f, .vertical = 2.0f}; // an item's margin
// An item's padding for mouse, pen and keyboard.
// The 11,8,11,9 touch padding is not used.
constexpr Thickness MenuPresenterItemPadding = Edges{.left = 11.0f, .top = 4.0f, .right = 11.0f, .bottom = 5.0f};
constexpr float     Placeholder              = 28.0f; // one check or icon column
constexpr float     MenuPresenterIconSize    = 16.0f; // the icon, scaled to fit
constexpr float     GlyphSize                = 12.0f; // the check, the radio bullet and the chevron
constexpr float     MenuPresenterShortcutGap = 24.0f; // the shortcut text's left margin
constexpr float     ShortcutTop              = 4.0f;  // ... and top margin
constexpr float     ChevronGap               = 24.0f; // the chevron's left margin
constexpr float     SeparatorSlot            = 3.0f;  // the 1px line and 1 clear above and below
constexpr float     SplitButtonWidth         = 38.0f; // a Split item's chevron button
constexpr float     SplitDivider             = 18.0f; // the divider before that button
constexpr float     SubmenuOverlap           = 4.0f;  // how far a submenu overlaps its parent
constexpr double    ShowDelayMs              = 400.0; // the submenu show delay
// The open slide's geometry. The lengths come from Theme::popup_motion.
constexpr float RootSlideRatio = 0.5f;  // a root menu's
constexpr float SubSlideRatio  = 0.67f; // a submenu's

constexpr char32_t MenuPresenterCheckGlyph   = 0xE73E; // a toggle item's check mark
constexpr char32_t RadioGlyph                = 0xE915; // a radio item's bullet
constexpr char32_t MenuPresenterChevronGlyph = 0xE974; // a submenu item's chevron

void draw_glyph(Renderer& renderer, const Theme& theme, char32_t codepoint, const Rect& box, const Color& color)
{
    draw_icon(renderer, IconGlyph{codepoint, theme.icon_font_family, GlyphSize}, box, color, GlyphSize, theme.icon_font_family);
}

bool is_check_kind(MenuItemKind kind)
{
    return kind == MenuItemKind::Toggle || kind == MenuItemKind::Radio;
}
} // namespace

// ---- automation nodes ----
// An item is drawn by its menu and has no Element, so each place in the menu gets one of these
// when a client first asks for it. It reads the item at its index live.
class MenuItemAutomation final : public Element, private InvokeProvider, private ToggleProvider, private ExpandCollapseProvider
{
public:
    MenuItemAutomation(MenuPresenter* owner, std::size_t index) : owner_(owner), index_(index)
    {
        set_parent_of(*this, owner);
    }

    void place()
    {
        detail::LayoutAccess::set_bounds(*this, index_ < owner_->count() ? owner_->item_rect(index_) : Rect{});
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        const MenuItem* entry = item();
        return entry != nullptr && entry->kind == MenuItemKind::Separator
                   ? AutomationRole::Separator
                   : AutomationRole::MenuItem;
    }
    // The submenu while it shows.
    void automation_children(std::vector<Element*>& out) const override
    {
        if (MenuPresenter* child = submenu())
        {
            out.push_back(child);
        }
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        const MenuItem* entry = item();
        return entry != nullptr ? entry->display_text() : std::string{};
    }
    void automation_providers(AutomationProviders& out) override
    {
        const MenuItem* entry = item();
        if (entry == nullptr)
        {
            return;
        }
        switch (entry->kind)
        {
            case MenuItemKind::Item:
                out.invoke = this;
                break;
            case MenuItemKind::Toggle:
            case MenuItemKind::Radio:
                out.toggle = this;
                break;
            case MenuItemKind::SubItem:
                out.expand_collapse = this;
                break;
            case MenuItemKind::Split:
                out.invoke          = this;
                out.expand_collapse = this;
                break;
            case MenuItemKind::Separator:
                break;
        }
    }
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    [[nodiscard]] MenuItem* item() const
    {
        return index_ < owner_->count() ? &(*owner_->items_)[index_] : nullptr;
    }
    [[nodiscard]] MenuPresenter* submenu() const
    {
        MenuPresenter* child = owner_->child_index_ == index_ ? owner_->open_child() : nullptr;
        return child != nullptr && child->visible() ? child : nullptr;
    }
    // A click on the item: runs it, toggles it or opens its submenu.
    bool click(MenuPresenter::Part part)
    {
        const MenuItem* entry = item();
        if (entry == nullptr || !entry->focusable() || !owner_->is_open())
        {
            return false;
        }
        owner_->activate(index_, part, true); // may close the menu and run a handler
        return true;
    }

    bool invoke() override
    {
        return click(MenuPresenter::Part::Whole);
    }
    [[nodiscard]] ToggleState toggle_value() const override
    {
        const MenuItem* entry = item();
        return entry != nullptr && entry->is_checked() ? ToggleState::On : ToggleState::Off;
    }
    bool toggle_next() override
    {
        return click(MenuPresenter::Part::Whole);
    }
    [[nodiscard]] bool expanded_state() const override
    {
        MenuPresenter* child = submenu();
        return child != nullptr && child->is_open();
    }
    bool set_expanded_state(bool expanded) override
    {
        const MenuItem* entry = item();
        if (entry == nullptr || !entry->focusable() || !entry->has_submenu() || !owner_->is_open())
        {
            return false;
        }
        if (expanded)
        {
            owner_->open_child(index_, true);
        }
        else if (expanded_state())
        {
            owner_->close_child(false);
        }
        return true;
    }

    MenuPresenter* owner_;
    std::size_t    index_;
};

MenuPresenter::MenuPresenter(MenuHost& host, MenuPresenter* parent) : host_(host), parent_(parent)
{
    if (parent_ != nullptr)
    {
        set_parent_of(*this, parent_);
    }
}

MenuPresenter::~MenuPresenter() = default;

MenuItemAutomation* MenuPresenter::item_automation(std::size_t index) const
{
    auto* self = const_cast<MenuPresenter*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    while (item_automation_.size() <= index)
    {
        item_automation_.push_back(std::make_unique<MenuItemAutomation>(self, item_automation_.size()));
        item_automation_.back()->place();
    }
    return item_automation_[index].get();
}

void MenuPresenter::place_item_automation()
{
    for (const auto& node : item_automation_)
    {
        node->place();
    }
}

void MenuPresenter::automation_children(std::vector<Element*>& out) const
{
    for (std::size_t i = 0; i < count(); ++i)
    {
        out.push_back(item_automation(i));
    }
}

Element* MenuPresenter::automation_focus() const
{
    const MenuPresenter* level = this;
    for (const MenuPresenter* child = level->open_child();
         child != nullptr && child->is_open() && child->focused_ < child->count();
         child = level->open_child())
    {
        level = child;
    }
    return level->focused_ < level->count() ? level->item_automation(level->focused_) : nullptr;
}

Element* MenuPresenter::automation_parent_of(const Element& child) const
{
    for (std::size_t i = 0; i < pool_.size(); ++i)
    {
        if (pool_[i].get() == &child)
        {
            return item_automation(i);
        }
    }
    return nullptr;
}

void MenuPresenter::set_items(std::vector<MenuItem>* items)
{
    for (auto& child : pool_)
    {
        if (child)
        {
            child->close_now();
            child->set_items(nullptr);
        }
    }
    items_        = items;
    child_index_  = detail::NoIndex;
    focused_      = detail::NoIndex;
    hovered_      = detail::NoIndex;
    pressed_      = detail::NoIndex;
    pending_open_ = detail::NoIndex;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

MenuPresenter* MenuPresenter::open_child() const
{
    return child_index_ < pool_.size() ? pool_[child_index_].get() : nullptr;
}

MenuPresenter* MenuPresenter::root()
{
    MenuPresenter* p = this;
    while (p->parent_ != nullptr)
    {
        p = p->parent_;
    }
    return p;
}

MenuPresenter* MenuPresenter::deepest_open()
{
    MenuPresenter* level = this;
    while (MenuPresenter* child = level->open_child())
    {
        if (!child->is_open())
        {
            break;
        }
        level = child;
    }
    return level;
}

void MenuPresenter::open(bool from_keyboard)
{
    close_child(false);
    phase_         = Phase::Opening;
    phase_ms_      = 0.0;
    hovered_       = detail::NoIndex;
    pressed_       = detail::NoIndex;
    key_pressed_   = false;
    pending_open_  = detail::NoIndex;
    pending_close_ = false;
    scroll_        = 0.0f;
    pointer_mode_  = !from_keyboard;
    focus_first();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    phase_changed();
}

void MenuPresenter::begin_close()
{
    if (phase_ == Phase::Closed || phase_ == Phase::Closing)
    {
        return;
    }
    close_child(false);
    phase_    = Phase::Closing;
    phase_ms_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
    phase_changed();
}

void MenuPresenter::close_now()
{
    close_child(false);
    phase_         = Phase::Closed;
    phase_ms_      = 0.0;
    hovered_       = detail::NoIndex;
    pressed_       = detail::NoIndex;
    key_pressed_   = false;
    pending_open_  = detail::NoIndex;
    pending_close_ = false;
    mark_dirty(DirtyFlags::Visual);
    phase_changed();
}

namespace
{
// A zero length is already over.
[[nodiscard]] double transition_fraction(double elapsed, Duration length)
{
    const double ms = length.count();
    return ms > 0.0 ? std::clamp(elapsed / ms, 0.0, 1.0) : 1.0;
}
} // namespace

float MenuPresenter::self_opacity() const
{
    switch (phase_)
    {
        case Phase::Closed:
            return 0.0f;
        case Phase::Closing:
            return 1.0f - static_cast<float>(transition_fraction(phase_ms_, motion_.close));
        case Phase::Opening:
            // USER32's menu fade (SPI_GETMENUFADE) blends the whole menu in. A slide shows it whole.
            if (motion_.menu == ThemePopupAnimation::Fade)
            {
                return static_cast<float>(transition_fraction(phase_ms_, motion_.open));
            }
            break;
        case Phase::Open:
            break;
    }
    return 1.0f;
}

void MenuPresenter::open_child(std::size_t index, bool from_keyboard)
{
    if (items_ == nullptr || index >= items_->size() || !(*items_)[index].has_submenu())
    {
        return;
    }
    pending_open_          = detail::NoIndex;
    pending_close_         = false;
    MenuPresenter* current = open_child();
    if (current != nullptr && child_index_ == index && current->is_open())
    {
        return;
    }
    if (current != nullptr)
    {
        current->close_now(); // a peer closes before the next opens
    }
    if (pool_.size() <= index)
    {
        pool_.resize(index + 1);
    }
    if (!pool_[index])
    {
        pool_[index] = std::make_unique<MenuPresenter>(host_, this);
    }
    MenuPresenter* child = pool_[index].get();
    child->set_items(&(*items_)[index].items);
    child_index_ = index;
    focused_     = index;
    child->open(from_keyboard);
    mark_dirty(DirtyFlags::Visual);
}

void MenuPresenter::close_child(bool animate)
{
    pending_close_       = false;
    MenuPresenter* child = open_child();
    if (child == nullptr)
    {
        return;
    }
    if (animate)
    {
        child->begin_close();
    }
    else
    {
        child->close_now();
    }
    mark_dirty(DirtyFlags::Visual);
}

void MenuPresenter::child_pointer_entered()
{
    // The pointer reaching the submenu cancels its pending close.
    pending_close_ = false;
    pending_open_  = detail::NoIndex;
    hovered_       = detail::NoIndex;
    if (parent_ != nullptr)
    {
        parent_->child_pointer_entered();
    }
}

bool MenuPresenter::keyboard_visual() const
{
    return !pointer_mode_ && host_.menu_keyboard_visual();
}

// ---- focus ----

void MenuPresenter::focus_first()
{
    focused_ = detail::NoIndex;
    for (std::size_t i = 0; i < count(); ++i)
    {
        if ((*items_)[i].focusable())
        {
            focused_ = i;
            return;
        }
    }
}

void MenuPresenter::move_focus(int step)
{
    // Moving focus wraps, and skips separators and disabled items. With
    // nothing focused, Down goes to the first item and Up to the last.
    const std::size_t n = count();
    if (n == 0)
    {
        return;
    }
    const auto size  = static_cast<long long>(n);
    long long  start = focused_ < n ? static_cast<long long>(focused_) : (step > 0 ? -1 : size);
    for (long long k = 1; k <= size; ++k)
    {
        const long long i = ((start + step * k) % size + size) % size;
        if ((*items_)[static_cast<std::size_t>(i)].focusable())
        {
            focused_ = static_cast<std::size_t>(i);
            scroll_into_view(focused_);
            mark_dirty(DirtyFlags::Visual);
            return;
        }
    }
}

void MenuPresenter::focus_changed(bool focused)
{
    focused_self_ = focused;
    key_pressed_  = false;
    mark_dirty(DirtyFlags::Visual);
}

// ---- layout ----

float MenuPresenter::row_height(std::size_t index) const
{
    return (*items_)[index].kind == MenuItemKind::Separator ? separator_slot_ : metrics_.row_height;
}

int MenuPresenter::item_state(std::size_t index, Part part) const
{
    // Pressed wins, then submenu open, then hover (the pointer, or keyboard focus).
    const MenuItem& item = (*items_)[index];
    if (!item.is_enabled())
    {
        return 0;
    }
    const MenuPresenter* child        = open_child();
    const bool           submenu_open = child != nullptr && child_index_ == index && child->is_open();
    const bool           split        = item.kind == MenuItemKind::Split;
    if ((pressed_ == index && hovered_ == index && (!split || pressed_part_ == part)) ||
        (key_pressed_ && focused_ == index && part == Part::Whole))
    {
        return 2;
    }
    if (submenu_open && (!split || part == Part::Secondary))
    {
        return 1;
    }
    if (hovered_ == index && (!split || hovered_part_ == part))
    {
        return 1;
    }
    if (keyboard_visual() && focused_ == index && hovered_ == detail::NoIndex && part == Part::Whole)
    {
        return 1;
    }
    return 0;
}

Size MenuPresenter::measure_override(Size available, const MeasureContext& context) const
{
    motion_                      = context.theme.popup_motion;
    const TextStyle& style       = context.theme.menu_text_style();
    const TextStyle& caption     = context.theme.type.caption;
    const auto&      era         = detail::controls(context.theme).era_menu;
    era_                         = era.has_value();
    padding_                     = era ? era->popup_padding : PresenterPadding;
    item_margin_                 = era ? Thickness{} : MenuPresenterItemMargin;
    separator_slot_              = era ? era->separator_height : SeparatorSlot;
    const Thickness item_padding = era ? era->item_padding : MenuPresenterItemPadding;
    metrics_                     = Metrics{};
    metrics_.row_height = context.measurer.measure("", style).height + item_padding.vertical() + item_margin_.vertical();

    // The menu-wide flags, from a scan of the items: any toggle or radio item adds
    // the check column to every item, any icon the icon column, any shortcut the shortcut column.
    for (std::size_t i = 0; i < count(); ++i)
    {
        const MenuItem& item = (*items_)[i];
        if (is_check_kind(item.kind))
        {
            metrics_.check_column = Placeholder;
        }
        const IconSource& icon = item.display_icon();
        if (!icon.empty() && item.kind != MenuItemKind::Separator)
        {
            (void)icon.measure(context.measurer);
            metrics_.icon_column = Placeholder;
        }
        if (item.kind != MenuItemKind::SubItem && item.kind != MenuItemKind::Separator)
        {
            const std::string shortcut = item.display_shortcut_text();
            if (!shortcut.empty())
            {
                metrics_.shortcut_width = std::max(metrics_.shortcut_width, context.measurer.measure(shortcut, caption).width);
            }
        }
        metrics_.any_submenu = metrics_.any_submenu || item.kind == MenuItemKind::SubItem || item.kind == MenuItemKind::Split;
    }

    float widest    = 0.0f;
    content_height_ = 0.0f;
    for (std::size_t i = 0; i < count(); ++i)
    {
        const MenuItem& item = (*items_)[i];
        content_height_ += row_height(i);
        if (item.kind == MenuItemKind::Separator)
        {
            continue;
        }
        const float label = context.measurer.measure(item.display_text(), style).width;
        if (era)
        {
            // The icon column, the label, the shortcut column shared by every item and the arrow's.
            float width = item_padding.horizontal() + era->icon_column + era->label_gap + label + era->arrow_column;
            if (item.kind == MenuItemKind::Split)
            {
                width += SplitButtonWidth;
            }
            else if (metrics_.shortcut_width > 0.0f)
            {
                width += era->shortcut_gap + metrics_.shortcut_width;
            }
            widest = std::max(widest, width);
            continue;
        }
        float width = item_margin_.horizontal() + item_padding.horizontal() + metrics_.check_column +
                      metrics_.icon_column + label;
        if (item.kind == MenuItemKind::SubItem || item.kind == MenuItemKind::Split)
        {
            width += ChevronGap + GlyphSize;
        }
        else if (metrics_.shortcut_width > 0.0f)
        {
            width += MenuPresenterShortcutGap + metrics_.shortcut_width;
        }
        widest = std::max(widest, width);
    }

    border_            = context.theme.border_width;
    const float border = border_;
    Size        desired{std::max(MenuPresenterMinWidth, widest + padding_.horizontal() + 2.0f * border),
                        std::max(MenuPresenterMinHeight, content_height_ + padding_.vertical() + 2.0f * border)};
    desired.height = std::min(desired.height, available.height);
    return desired;
}

Rect MenuPresenter::viewport() const
{
    const float border = border_;
    return Rect{bounds().x + border + padding_.left,
                bounds().y + border + padding_.top,
                std::max(0.0f, bounds().width - 2.0f * border - padding_.horizontal()),
                std::max(0.0f, bounds().height - 2.0f * border - padding_.vertical())};
}

Rect MenuPresenter::slot_rect(std::size_t index) const
{
    const Rect view = viewport();
    float      y    = view.y - scroll_;
    for (std::size_t i = 0; i < index; ++i)
    {
        y += row_height(i);
    }
    return Rect{view.x, y, view.width, row_height(index)};
}

Rect MenuPresenter::item_rect(std::size_t index) const
{
    return slot_rect(index).inset(item_margin_);
}

Rect MenuPresenter::split_button_rect(std::size_t index) const
{
    const Rect item = item_rect(index);
    return Rect{item.right() - SplitButtonWidth, item.y, SplitButtonWidth, item.height};
}

std::size_t MenuPresenter::hit_test(Point p, Part* part) const
{
    if (items_ == nullptr || !viewport().contains(p))
    {
        return detail::NoIndex;
    }
    for (std::size_t i = 0; i < count(); ++i)
    {
        if (!slot_rect(i).contains(p))
        {
            continue;
        }
        if ((*items_)[i].kind == MenuItemKind::Separator)
        {
            return detail::NoIndex;
        }
        if (part != nullptr)
        {
            *part = (*items_)[i].kind == MenuItemKind::Split && split_button_rect(i).contains(p)
                        ? Part::Secondary
                        : Part::Whole;
        }
        return i;
    }
    return detail::NoIndex;
}

void MenuPresenter::scroll_into_view(std::size_t index)
{
    if (index >= count())
    {
        return;
    }
    const Rect view = viewport();
    const Rect slot = slot_rect(index);
    if (slot.top() < view.top())
    {
        scroll_ -= view.top() - slot.top();
    }
    else if (slot.bottom() > view.bottom())
    {
        scroll_ += slot.bottom() - view.bottom();
    }
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content_height_ - view.height));
}

// ---- render ----

void MenuPresenter::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    // The open slide: the menu slides out from its own edge nearest what it hangs
    // from (from above for one below it, from below for one above), clipped to where it ends up.
    motion_      = theme.popup_motion;
    float offset = 0.0f;
    if (phase_ == Phase::Opening && motion_.menu == ThemePopupAnimation::Slide)
    {
        const float progress = static_cast<float>(transition_fraction(phase_ms_, motion_.open));
        const float eased    = easing::evaluate(easing::ControlFastOutSlowIn, progress);
        const float ratio    = parent_ != nullptr ? SubSlideRatio : RootSlideRatio;
        offset               = (1.0f - eased) * ratio * bounds().height * (slides_up_ ? 1.0f : -1.0f);
    }
    const bool sliding = offset != 0.0f;
    if (sliding)
    {
        renderer.push_clip(bounds());
        renderer.push_transform(Matrix::translation(0.0f, offset));
    }

    if (const auto& era = detail::controls(theme).era_menu)
    {
        render_era(renderer, theme, *era);
        if (sliding)
        {
            renderer.pop_transform();
            renderer.pop_clip();
        }
        return;
    }

    const float radius = theme.overlay_corner_radius;
    fill_overlay_surface(renderer, theme, bounds(), radius, theme.colors().overlay);

    const Rect view = viewport();
    renderer.push_clip(view);
    const TextStyle& base_style = theme.menu_text_style();
    TextStyle        text_style = base_style;
    text_style.horizontal       = TextAlign::Leading;
    TextStyle shortcut_style    = theme.type.caption;
    shortcut_style.horizontal   = TextAlign::Trailing;

    const MenuPresenter* child      = open_child();
    const bool           show_focus = keyboard_visual() && (child == nullptr || !child->is_open());

    for (std::size_t i = 0; i < count(); ++i)
    {
        const MenuItem& item = (*items_)[i];
        const Rect      slot = slot_rect(i);
        if (slot.bottom() < view.top() || slot.top() > view.bottom())
        {
            continue;
        }
        if (item.kind == MenuItemKind::Separator)
        {
            // A separator: a 1px rule across the menu, 1px clear above and below.
            const Rect line{view.x, slot.y + 1.0f, view.width, 1.0f};
            if (theme.divider_visual == ThemeDividerVisual::FlatStroke)
            {
                renderer.fill_rounded_rect(line, 0.0f, theme.colors().divider);
            }
            else
            {
                renderer.fill_rounded_rect(line, 0.0f, theme.surfaces.separator_dark);
                renderer.fill_rounded_rect(Rect{line.x, line.y + 1.0f, line.width, 1.0f}, 0.0f, theme.surfaces.separator_light);
            }
            continue;
        }

        const bool enabled = item.is_enabled();
        const Rect rect    = item_rect(i);

        const auto part_state = [&](Part part) { return item_state(i, part); };
        auto       fill_for   = [&](int state) -> Color
        {
            return state == 2 ? theme.surfaces.menu_pressed.value_or(theme.surfaces.menu_highlight)
                              : theme.surfaces.menu_highlight;
        };

        int whole_state = part_state(Part::Whole);
        if (item.kind == MenuItemKind::Split)
        {
            const Rect button = split_button_rect(i);
            const Rect primary{rect.x, rect.y, std::max(0.0f, rect.width - button.width), rect.height};
            const int  second = part_state(Part::Secondary);
            if (whole_state != 0)
            {
                renderer.fill_rounded_rect(primary, theme.corner_radius, fill_for(whole_state));
            }
            if (second != 0)
            {
                renderer.fill_rounded_rect(button, theme.corner_radius, fill_for(second));
            }
            renderer.fill_rounded_rect(Rect{button.x - 1.0f, rect.center().y - SplitDivider * 0.5f, 1.0f, SplitDivider},
                                       0.0f,
                                       theme.colors().divider);
            whole_state = std::max(whole_state, second);
        }
        else if (whole_state != 0)
        {
            renderer.fill_rounded_rect(rect, theme.corner_radius, fill_for(whole_state));
        }

        const bool  hot  = whole_state != 0;
        const Color text = !enabled ? theme.colors().text_disabled
                                    : (hot ? theme.surfaces.menu_highlight_text : theme.surfaces.menu_text);
        const Color secondary = !enabled ? theme.colors().text_disabled : theme.surfaces.menu_secondary_text.value_or(text);

        const Rect content = rect.inset(MenuPresenterItemPadding);
        float      x       = content.x;
        if (metrics_.check_column > 0.0f)
        {
            if (is_check_kind(item.kind) && item.is_checked())
            {
                draw_glyph(renderer,
                           theme,
                           item.kind == MenuItemKind::Radio ? RadioGlyph : MenuPresenterCheckGlyph,
                           Rect{x, content.y, GlyphSize, content.height},
                           text);
            }
            x += metrics_.check_column;
        }
        if (metrics_.icon_column > 0.0f)
        {
            const IconSource& icon = item.display_icon();
            if (!icon.empty())
            {
                icon.draw(renderer,
                          Rect{x, content.center().y - MenuPresenterIconSize * 0.5f, MenuPresenterIconSize, MenuPresenterIconSize},
                          text);
            }
            x += metrics_.icon_column;
        }

        float text_right = content.right();
        if (item.kind == MenuItemKind::SubItem)
        {
            // The classic packs draw Marlett's cascade arrow.
            draw_disclosure(renderer,
                            theme,
                            IconGlyph{MenuPresenterChevronGlyph, theme.icon_font_family, GlyphSize},
                            ArrowDirection::Right,
                            Rect{content.right() - GlyphSize, content.y, GlyphSize, content.height},
                            secondary,
                            GlyphSize);
            text_right -= ChevronGap + GlyphSize;
        }
        else if (item.kind == MenuItemKind::Split)
        {
            draw_disclosure(renderer,
                            theme,
                            IconGlyph{MenuPresenterChevronGlyph, theme.icon_font_family, GlyphSize},
                            ArrowDirection::Right,
                            split_button_rect(i),
                            secondary,
                            GlyphSize);
            text_right = split_button_rect(i).x;
        }
        else if (metrics_.shortcut_width > 0.0f)
        {
            const std::string shortcut = item.display_shortcut_text();
            if (!shortcut.empty())
            {
                const float top = is_check_kind(item.kind) ? 0.0f : ShortcutTop;
                renderer.draw_text(shortcut,
                                   Rect{content.right() - metrics_.shortcut_width,
                                        content.y + top,
                                        metrics_.shortcut_width,
                                        std::max(0.0f, content.height - top)},
                                   secondary,
                                   shortcut_style);
            }
            text_right -= MenuPresenterShortcutGap + metrics_.shortcut_width;
        }
        renderer.draw_text(item.display_text(),
                           Rect{x, content.y, std::max(0.0f, text_right - x), content.height},
                           text,
                           text_style);

        if (show_focus && focused_ == i && enabled)
        {
            // Inside the item, as every item in a viewport is ringed.
            const FocusRingStyle ring = theme.focus_ring_style();
            draw_focus_ring(renderer, rect.inset(Thickness{ring.outward_extent()}), theme.corner_radius, ring);
        }
    }
    renderer.pop_clip();

    stroke_rounded_rect_inside(renderer,
                               bounds(),
                               radius,
                               theme.surfaces.menu_border.value_or(theme.surfaces.field_border),
                               theme.border_width);
    if (sliding)
    {
        renderer.pop_transform();
        renderer.pop_clip();
    }
}

// The Aero menu: a square #F5F5F5 popup, the gutter and its two lines
// under the icon column, rows with a glossy hot state and no pressed one, a checked item's
// boxed mark in place of its icon, the cascade arrow in its own column, and a two-line
// separator from the gutter's edge. Keyboard focus shows as the hot row, as in a Win32 menu.
void MenuPresenter::render_era(Renderer& renderer, const Theme& theme, const ControlChrome::EraMenu& menu) const
{
    renderer.fill_rounded_rect(bounds(), 0.0f, theme.surfaces.menu);
    const Rect  inner = bounds().inset(Thickness{border_});
    const float lines = std::max(0.0f, inner.height - 4.0f);
    renderer.fill_rounded_rect(Rect{inner.x + 1.0f, inner.y + 2.0f, menu.gutter_width, lines}, 2.0f, menu.gutter);
    const float edge = inner.x + 1.0f + menu.gutter_width;
    renderer.fill_rounded_rect(Rect{edge, inner.y + 2.0f, 1.0f, lines}, 0.0f, menu.gutter_dark);
    renderer.fill_rounded_rect(Rect{edge + 1.0f, inner.y + 2.0f, 1.0f, lines}, 0.0f, menu.gutter_light);

    const Rect view = viewport();
    renderer.push_clip(view);
    TextStyle text_style      = theme.menu_text_style();
    text_style.horizontal     = TextAlign::Leading;
    TextStyle shortcut_style  = theme.type.caption;
    shortcut_style.horizontal = TextAlign::Trailing;

    const auto hot = [&](const Rect& r)
    {
        renderer.fill_rounded_rect_gradient(r, 2.0f, LinearGradientBrush{menu.hot_fill, GradientAxis::Vertical});
        renderer.stroke_rounded_rect(r.inset(Thickness{0.5f}), 2.0f, menu.hot_stroke, 1.0f);
        renderer.stroke_rounded_rect(r.inset(Thickness{1.5f}), 1.5f, menu.hot_inner, 1.0f);
    };
    // The 4 x 7 cascade arrow, its left edge `left`.
    const auto arrow = [&](float left, const Rect& row, const Color& color)
    {
        const float y = std::round(row.center().y - 3.5f);
        renderer.fill_polygon(FillPolygon{{Point{left, y}, Point{left + 4.0f, y + 3.5f}, Point{left, y + 7.0f}}, color});
    };

    for (std::size_t i = 0; i < count(); ++i)
    {
        const MenuItem& item = (*items_)[i];
        const Rect      slot = slot_rect(i);
        if (slot.bottom() < view.top() || slot.top() > view.bottom())
        {
            continue;
        }
        if (item.kind == MenuItemKind::Separator)
        {
            const float y     = slot.y + menu.separator_top;
            const float left  = edge + 1.0f;
            const float width = std::max(0.0f, inner.right() - 1.0f - left);
            renderer.fill_rounded_rect(Rect{left, y, width, 1.0f}, 0.0f, menu.separator_dark);
            renderer.fill_rounded_rect(Rect{left, y + 1.0f, width, 1.0f}, 0.0f, menu.separator_light);
            continue;
        }

        const bool enabled = item.is_enabled();
        const Rect rect    = item_rect(i);
        if (item.kind == MenuItemKind::Split)
        {
            const Rect button = split_button_rect(i);
            if (item_state(i, Part::Whole) != 0)
            {
                hot(Rect{rect.x, rect.y, std::max(0.0f, rect.width - button.width), rect.height});
            }
            if (item_state(i, Part::Secondary) != 0)
            {
                hot(button);
            }
        }
        else if (item_state(i, Part::Whole) != 0)
        {
            hot(rect);
        }

        const Color text    = enabled ? theme.surfaces.menu_text : menu.disabled_text;
        const Rect  content = rect.inset(menu.item_padding);
        const Rect  column{content.x, rect.y, menu.icon_column, rect.height};
        if (is_check_kind(item.kind) && item.is_checked())
        {
            // The mark's box: 22 square, inset 1, as tall as the row allows; the check path, or a bullet.
            const float side = std::min(22.0f, rect.height - 2.0f);
            const Rect panel{std::round(column.center().x - side * 0.5f), std::round(column.center().y - side * 0.5f), side, side};
            renderer.fill_rounded_rect(panel, 3.0f, enabled ? menu.check_panel : menu.check_panel_disabled);
            renderer.stroke_rounded_rect(panel.inset(Thickness{0.5f}),
                                         3.0f,
                                         enabled ? menu.check_panel_border : menu.check_panel_border_disabled,
                                         1.0f);
            const Color mark = enabled ? menu.check : menu.check_disabled;
            if (item.kind == MenuItemKind::Radio)
            {
                renderer.fill_ellipse(panel.center(), 3.0f, 3.0f, mark);
            }
            else
            {
                const Point o{std::round(panel.center().x - 4.6f), std::round(panel.center().y - 5.4f)};
                renderer.fill_polygon(
                    FillPolygon{{Point{o.x, o.y + 5.1f},
                                 Point{o.x + 1.7f, o.y + 5.2f},
                                 Point{o.x + 3.4f, o.y + 7.1f},
                                 Point{o.x + 8.0f, o.y + 0.4f},
                                 Point{o.x + 9.2f, o.y},
                                 Point{o.x + 3.3f, o.y + 10.8f}},
                                mark});
            }
        }
        else if (const IconSource& icon = item.display_icon(); !icon.empty())
        {
            icon.draw(renderer,
                      Rect{column.center().x - MenuPresenterIconSize * 0.5f,
                           column.center().y - MenuPresenterIconSize * 0.5f,
                           MenuPresenterIconSize,
                           MenuPresenterIconSize},
                      text);
        }

        const float left       = content.x + menu.icon_column + menu.label_gap;
        float       text_right = content.right() - menu.arrow_column;
        if (item.kind == MenuItemKind::SubItem)
        {
            arrow(content.right() - menu.arrow_column + 4.0f, rect, text);
        }
        else if (item.kind == MenuItemKind::Split)
        {
            const Rect button = split_button_rect(i);
            arrow(std::round(button.center().x - 2.0f), rect, text);
            text_right = button.x;
        }
        else if (metrics_.shortcut_width > 0.0f)
        {
            const std::string shortcut = item.display_shortcut_text();
            if (!shortcut.empty())
            {
                renderer
                    .draw_text(shortcut,
                               Rect{text_right - metrics_.shortcut_width, content.y, metrics_.shortcut_width, content.height},
                               text,
                               shortcut_style);
            }
            text_right -= menu.shortcut_gap + metrics_.shortcut_width;
        }
        renderer.draw_text(item.display_text(),
                           Rect{left, content.y, std::max(0.0f, text_right - left), content.height},
                           text,
                           text_style);
    }
    renderer.pop_clip();
    stroke_rounded_rect_inside(renderer, bounds(), 0.0f, theme.surfaces.menu_border.value_or(theme.surfaces.field_border), border_);
}

// ---- input ----

bool MenuPresenter::pointer_override(const PointerEvent& e)
{
    if (items_ == nullptr || phase_ == Phase::Closed || phase_ == Phase::Closing)
    {
        return false;
    }
    Part              part  = Part::Whole;
    const std::size_t index = hit_test(e.position, &part);
    switch (e.action)
    {
        case PointerAction::Move:
        {
            if (!bounds().contains(e.position))
            {
                if (hovered_ != detail::NoIndex)
                {
                    hovered_ = detail::NoIndex;
                    mark_dirty(DirtyFlags::Visual);
                }
                pending_open_ = detail::NoIndex;
                break;
            }
            pointer_mode_ = true;
            if (parent_ != nullptr)
            {
                parent_->child_pointer_entered();
            }
            if (index != hovered_ || part != hovered_part_)
            {
                mark_dirty(DirtyFlags::Visual);
            }
            hovered_      = index;
            hovered_part_ = part;
            if (index == detail::NoIndex)
            {
                break;
            }
            const MenuItem& item = (*items_)[index];
            if (item.focusable())
            {
                focused_ = index; // the item under the pointer takes focus
            }
            const bool     opens      = item.has_submenu() && item.is_enabled() &&
                                        (item.kind == MenuItemKind::SubItem || part == Part::Secondary);
            MenuPresenter* child      = open_child();
            const bool     child_open = child != nullptr && child->is_open();
            if (opens)
            {
                if (child_open && child_index_ == index)
                {
                    pending_close_ = false;
                    pending_open_  = detail::NoIndex;
                }
                else if (pending_open_ != index)
                {
                    pending_open_ = index;
                    open_wait_ms_ = 0.0;
                }
            }
            else
            {
                pending_open_ = detail::NoIndex;
                if (child_open && !pending_close_)
                {
                    pending_close_ = true;
                    close_wait_ms_ = 0.0;
                }
            }
            break;
        }
        case PointerAction::Down:
            if (e.button == PointerButton::Left && index != detail::NoIndex && (*items_)[index].focusable())
            {
                pressed_      = index;
                pressed_part_ = part;
                hovered_      = index;
                hovered_part_ = part;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                const bool clicked = pressed_ != detail::NoIndex && pressed_ == index && pressed_part_ == part;
                pressed_           = detail::NoIndex;
                mark_dirty(DirtyFlags::Visual);
                if (clicked)
                {
                    activate(index, part, false); // may close everything and run a handler: last
                    return false;
                }
            }
            break;
        case PointerAction::Leave:
            hovered_      = detail::NoIndex;
            pressed_      = detail::NoIndex;
            pending_open_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            break;
        case PointerAction::Wheel:
            break;
    }
    return false;
}

bool MenuPresenter::wheel_override(const PointerEvent& e)
{
    const Rect view = viewport();
    if (!bounds().contains(e.position) || content_height_ <= view.height)
    {
        return false;
    }
    scroll_ = std::clamp(scroll_ + e.wheel_delta, 0.0f, content_height_ - view.height);
    mark_dirty(DirtyFlags::Visual);
    return true;
}

void MenuPresenter::activate(std::size_t index, Part part, bool from_keyboard)
{
    if (items_ == nullptr || index >= items_->size())
    {
        return;
    }
    MenuItem& item = (*items_)[index];
    if (!item.focusable())
    {
        return;
    }
    focused_ = index;
    if (item.kind == MenuItemKind::SubItem || (item.kind == MenuItemKind::Split && part == Part::Secondary))
    {
        MenuPresenter* child = open_child();
        if (item.kind == MenuItemKind::Split && child != nullptr && child_index_ == index && child->is_open())
        {
            close_child(true); // a Split item's chevron button toggles
        }
        else
        {
            open_child(index, from_keyboard);
        }
        return;
    }
    host_.menu_invoke(item); // last: this closes the menu and may run a handler that tears it down
}

bool MenuPresenter::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    (void)route_key(e);
    return handled;
}

bool MenuPresenter::route_key(const KeyEvent& e)
{
    if (items_ == nullptr || !is_open())
    {
        return false;
    }
    MenuPresenter* level = deepest_open();
    if (e.action == KeyAction::Down)
    {
        level->pointer_mode_ = false;
        level->hovered_      = detail::NoIndex;
    }
    return level->handle_level_key(e);
}

bool MenuPresenter::handle_level_key(const KeyEvent& e)
{
    // The menu keys, for the level holding focus.
    if (e.action == KeyAction::Up)
    {
        if ((e.key == Key::Enter || e.key == Key::Space) && key_pressed_)
        {
            key_pressed_ = false;
            mark_dirty(DirtyFlags::Visual);
            if (focused_ < count())
            {
                activate(focused_, Part::Whole, true); // last
            }
            return true;
        }
        return false;
    }
    if (key_pressed_ && e.key != Key::Enter && e.key != Key::Space)
    {
        key_pressed_ = false;
    }
    switch (e.key)
    {
        case Key::ArrowDown:
            move_focus(+1);
            return true;
        case Key::ArrowUp:
            move_focus(-1);
            return true;
        case Key::ArrowRight:
            if (focused_ < count() && (*items_)[focused_].has_submenu() && (*items_)[focused_].is_enabled())
            {
                open_child(focused_, true);
                return true;
            }
            return false;
        case Key::ArrowLeft:
            if (parent_ != nullptr)
            {
                parent_->close_child(true); // focus goes back to the item that opened it
                parent_->pointer_mode_ = false;
                return true;
            }
            return false;
        case Key::Enter:
        case Key::Space:
            if (focused_ < count())
            {
                const MenuItem& item = (*items_)[focused_];
                if (item.kind == MenuItemKind::SubItem && item.has_submenu() && item.is_enabled())
                {
                    open_child(focused_, true);
                    return true;
                }
                key_pressed_ = item.focusable();
                mark_dirty(DirtyFlags::Visual);
            }
            return true;
        case Key::Tab:
            return true; // a menu swallows Tab
        default:
            return false;
    }
}

// ---- time ----

void MenuPresenter::tick_override(Duration delta)
{
    if (phase_ == Phase::Opening)
    {
        phase_ms_ += animation_delta(delta).count();
        if (motion_.menu == ThemePopupAnimation::None || phase_ms_ >= motion_.open.count())
        {
            phase_    = Phase::Open;
            phase_ms_ = 0.0;
        }
        mark_dirty(DirtyFlags::Visual);
    }
    else if (phase_ == Phase::Closing)
    {
        phase_ms_ += animation_delta(delta).count();
        if (phase_ms_ >= motion_.close.count())
        {
            phase_    = Phase::Closed;
            phase_ms_ = 0.0;
            phase_changed();
        }
        mark_dirty(DirtyFlags::Visual);
    }
    if (pending_open_ != detail::NoIndex)
    {
        open_wait_ms_ += delta.count();
        if (open_wait_ms_ >= ShowDelayMs)
        {
            open_child(pending_open_, false);
        }
    }
    if (pending_close_)
    {
        close_wait_ms_ += delta.count();
        if (close_wait_ms_ >= ShowDelayMs)
        {
            close_child(true);
        }
    }
    // A hover delay still running: one wake when it is up.
    if (pending_open_ != detail::NoIndex)
    {
        request_tick_in(Duration{ShowDelayMs - open_wait_ms_});
    }
    if (pending_close_)
    {
        request_tick_in(Duration{ShowDelayMs - close_wait_ms_});
    }
    if (MenuPresenter* child = open_child(); child != nullptr && child->visible())
    {
        child->tick(delta);
    }
}

void MenuPresenter::clear_dirty_override(DirtyFlags flags)
{
    for (auto& child : pool_)
    {
        if (child)
        {
            clear_dirty_of(*child, flags);
        }
    }
}

// ---- the submenu popup ----

void MenuPresenter::sync_popup()
{
    MenuPresenter* child = open_child();
    if (child == nullptr || !child->visible())
    {
        close_popup();
        return;
    }
    open_popup({
        .content       = child,
        .light_dismiss = child->is_open(),
        .place         = [this](Element& content, Size client, const MeasureContext& context)
        { return place_submenu(content, client, context); },
        .on_dismiss = [this](PopupCloseReason) { close_child(true); },
    });
}

float MenuPresenter::transition_opacity() const
{
    const MenuPresenter* child = open_child();
    return child != nullptr ? child->self_opacity() : 1.0f;
}

bool MenuPresenter::place_submenu(Element& content, Size client, const MeasureContext& context)
{
    MenuPresenter* child = open_child();
    if (child == nullptr || &content != child || child_index_ >= count())
    {
        return false;
    }
    // The submenu's placement. The overlap is measured from the item's slot, margins included.
    const Rect  owner   = slot_rect(child_index_);
    const Size  desired = content.measure(client, context);
    const float width   = std::min(desired.width, client.width);
    const float height  = std::min(desired.height, client.height);
    float       x       = owner.right() - SubmenuOverlap;
    if (x + width > client.width)
    {
        x = owner.left() - width + SubmenuOverlap >= 0.0f ? owner.left() - width + SubmenuOverlap : client.width - width;
    }
    float y  = owner.top();
    bool  up = false;
    if (y + height > client.height)
    {
        y  = owner.bottom() - height >= 0.0f ? owner.bottom() - height : 0.0f;
        up = true;
    }
    child->set_slides_up(up);
    content.arrange(Rect{std::max(0.0f, x), std::max(0.0f, y), width, height});
    return true;
}

} // namespace ui::detail

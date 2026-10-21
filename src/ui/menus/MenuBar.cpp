// SPDX-License-Identifier: MIT
#include "winui/ui/menus/MenuBar.hpp"

#include "MenuInvoke.hpp"
#include "MenuPresenter.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace
{
// MenuBar metrics.
constexpr float     BarMinHeight      = 40.0f;                                            // the bar's minimum height
constexpr Thickness MenuHeaderPadding = Symmetric{.horizontal = 10.0f, .vertical = 4.0f}; // a header's padding
constexpr Thickness MenuHeaderMargin{4.0f};                                               // a header's margin
} // namespace

class MenuBarControl::Host final : public detail::MenuHost
{
public:
    explicit Host(MenuBarControl& owner) : owner_(owner)
    {
    }

    void menu_invoke(MenuItem& item) override
    {
        MenuBarControl&   owner = owner_;
        const std::size_t index = owner.open_index_;
        if (index >= owner.items_.size())
        {
            return;
        }
        invoke_menu_item(owner.items_[index].items,
                         item,
                         [&owner]
                         {
                             owner.close();
                             if (Window* window = owner.window())
                             {
                                 (void)window->end_menu_access(); // the command runs where focus was
                             }
                         });
    }

    [[nodiscard]] bool menu_keyboard_visual() const override
    {
        return owner_.focus_visible();
    }

private:
    MenuBarControl& owner_;
};

// ---- automation nodes ----
// A header is drawn by the bar and has no Element, so each one gets one of these when a client
// first asks for it. Expanding it opens the menu, which is then its child.
class MenuBarHeaderAutomation final : public Element, private ExpandCollapseProvider
{
public:
    MenuBarHeaderAutomation(MenuBarControl* owner, std::size_t index) : owner_(owner), index_(index)
    {
        set_parent_of(*this, owner);
    }

    void place()
    {
        detail::LayoutAccess::set_bounds(*this, index_ < owner_->items_.size() ? owner_->header_visual(index_) : Rect{});
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::MenuItem;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        if (owner_->open_index_ == index_ && owner_->presenter_ && owner_->presenter_->visible())
        {
            out.push_back(owner_->presenter_.get());
        }
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return index_ < owner_->items_.size() ? owner_->items_[index_].text : std::string{};
    }
    void automation_providers(AutomationProviders& out) override
    {
        if (index_ < owner_->items_.size() && !owner_->items_[index_].items.empty())
        {
            out.expand_collapse = this;
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
    [[nodiscard]] bool expanded_state() const override
    {
        return owner_->open_index_ == index_;
    }
    bool set_expanded_state(bool expanded) override
    {
        if (!owner_->is_enabled() || index_ >= owner_->items_.size())
        {
            return false;
        }
        if (expanded)
        {
            owner_->open(index_, true);
        }
        else if (owner_->open_index_ == index_)
        {
            owner_->close();
        }
        return true;
    }

    MenuBarControl* owner_;
    std::size_t     index_;
};

bool MenuBarControl::take_menu_access(char32_t letter)
{
    if (!is_enabled() || items_.empty())
    {
        return false;
    }
    if (letter == 0)
    {
        if (!focus())
        {
            return false;
        }
        focused_header_ = 0;
        mark_dirty(DirtyFlags::Visual);
        return true;
    }
    const TextAnalyzer& analyzer = text_analyzer();
    const std::string   wanted   = analyzer.to_lower(encode_utf8(letter));
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const std::string& text = items_[i].text;
        if (!text.empty() && analyzer.to_lower(text.substr(0, next_code_point(text, 0))) == wanted)
        {
            if (!focus())
            {
                return false;
            }
            open(i, true);
            return true;
        }
    }
    return false;
}

MenuBarHeaderAutomation* MenuBarControl::header_automation(std::size_t index) const
{
    auto* self = const_cast<MenuBarControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    while (header_automation_.size() <= index)
    {
        header_automation_.push_back(std::make_unique<MenuBarHeaderAutomation>(self, header_automation_.size()));
        header_automation_.back()->place();
    }
    return header_automation_[index].get();
}

void MenuBarControl::automation_children(std::vector<Element*>& out) const
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        out.push_back(header_automation(i));
    }
}

Element* MenuBarControl::automation_focus() const
{
    if (open_index_ < items_.size() && presenter_ && presenter_->is_open())
    {
        if (Element* item = presenter_->automation_focus())
        {
            return item;
        }
    }
    return focused_header_ < items_.size() ? header_automation(focused_header_) : nullptr;
}

Element* MenuBarControl::automation_parent_of(const Element& child) const
{
    if (&child == presenter_.get() && open_index_ < items_.size())
    {
        return header_automation(open_index_);
    }
    return nullptr;
}

MenuBarControl::MenuBarControl() : host_(std::make_unique<Host>(*this)), checks_(std::make_unique<detail::MenuChecks>())
{
    checks_->changed = [this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); };
}

MenuBarControl::~MenuBarControl() = default;

// Only moved before it is parented, so only the items carry over: the host is rebound and the
// presenter is rebuilt on the next open.
// Element's move takes only the base subobject, so other's own members are still intact.
// NOLINTBEGIN(bugprone-use-after-move)
MenuBarControl::MenuBarControl(MenuBarControl&& other) noexcept :
    Element(std::move(other)),
    items_(std::move(other.items_)),
    host_(std::make_unique<Host>(*this)),
    checks_(std::move(other.checks_)) // the items moved with their buffers, so the subscriptions still point at them
{
    checks_->changed = [this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); };
}
// NOLINTEND(bugprone-use-after-move)

void MenuBarControl::set_items(std::vector<MenuBarItem> items)
{
    if (presenter_)
    {
        presenter_->close_now();
        presenter_->set_items(nullptr);
    }
    checks_->subscriptions.clear(); // they point into the items about to go
    items_ = std::move(items);
    for (MenuBarItem& menu : items_)
    {
        follow_menu_bindings(menu.items, *checks_);
    }
    open_index_          = detail::NoIndex;
    closing_index_       = detail::NoIndex;
    closing_ms_          = -1.0;
    hovered_header_      = detail::NoIndex;
    pointer_down_header_ = detail::NoIndex;
    focused_header_      = 0;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    sync_popup();
}

void MenuBarControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_header_      = detail::NoIndex;
        pointer_down_header_ = detail::NoIndex;
        close();
    }
    mark_dirty(DirtyFlags::Visual);
}

void MenuBarControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        close();
    }
    mark_dirty(DirtyFlags::Visual);
}

std::optional<std::size_t> MenuBarControl::active_command() const
{
    if (open_index_ == detail::NoIndex || !presenter_ || !presenter_->is_open())
    {
        return std::nullopt;
    }
    return detail::optional_index(presenter_->focused_index());
}

detail::MenuPresenter& MenuBarControl::presenter()
{
    if (!presenter_)
    {
        presenter_ = std::make_unique<detail::MenuPresenter>(*host_, nullptr);
        set_parent_of(*presenter_, this);
    }
    return *presenter_;
}

void MenuBarControl::sync_popup()
{
    const std::size_t shown = open_index_ != detail::NoIndex ? open_index_ : closing_index_;
    if (!presenter_ || (open_index_ == detail::NoIndex && closing_ms_ < 0.0) || shown >= items_.size())
    {
        close_popup();
        return;
    }
    const Rect header = header_visual(shown);
    open_popup({
        .content     = presenter_.get(),
        .anchor_rect = Rect{header.x - bounds().x, header.y - bounds().y, header.width, header.height},
        .on_dismiss  = [this](PopupCloseReason) { close(); },
    });
}

void MenuBarControl::arrange_override(const Rect& /*final_rect*/, const ArrangeContext& /*context*/)
{
    sync_popup();
    for (const auto& header : header_automation_)
    {
        header->place();
    }
}

void MenuBarControl::open(std::size_t index, bool from_keyboard)
{
    if (index >= items_.size() || !is_enabled())
    {
        return;
    }
    // An empty menu does not open.
    focused_header_ = index;
    if (items_[index].items.empty())
    {
        close();
        mark_dirty(DirtyFlags::Visual);
        return;
    }
    (void)presenter();
    open_index_    = index;
    closing_index_ = detail::NoIndex;
    closing_ms_    = -1.0;
    presenter_->set_items(&items_[index].items);
    presenter_->set_slide_anchor(header_visual(index).center().y);
    presenter_->open(from_keyboard);
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void MenuBarControl::close()
{
    if (open_index_ == detail::NoIndex)
    {
        return;
    }
    closing_index_ = open_index_;
    open_index_    = detail::NoIndex;
    closing_ms_    = 0.0;
    if (presenter_)
    {
        presenter_->close_child(false);
    }
    // Visual only: the bar's size never changes with a menu open.
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

double MenuBarControl::close_ms() const
{
    return presenter_ ? presenter_->motion().close.count() : 83.0;
}

float MenuBarControl::transition_opacity() const
{
    if (open_index_ != detail::NoIndex)
    {
        return presenter_ ? presenter_->self_opacity() : 1.0f;
    }
    if (closing_ms_ >= 0.0)
    {
        const double length = close_ms();
        return length > 0.0 ? 1.0f - static_cast<float>(std::clamp(closing_ms_ / length, 0.0, 1.0)) : 0.0f;
    }
    return 0.0f;
}

void MenuBarControl::tick_override(Duration delta)
{
    if (closing_ms_ >= 0.0)
    {
        closing_ms_ += animation_delta(delta).count();
        if (closing_ms_ >= close_ms())
        {
            closing_ms_    = -1.0;
            closing_index_ = detail::NoIndex;
            if (presenter_)
            {
                presenter_->close_now();
            }
            sync_popup();
        }
        mark_dirty(DirtyFlags::Visual);
    }
    if (presenter_ && presenter_->visible())
    {
        presenter_->tick(delta);
    }
}

void MenuBarControl::clear_dirty_override(DirtyFlags flags)
{
    if (presenter_)
    {
        clear_dirty_of(*presenter_, flags);
    }
}

Size MenuBarControl::measure_override(Size available, const MeasureContext& context) const
{
    // Menus are chrome: an era with its own chrome font (Theme::chrome_text_style) uses it here.
    const TextStyle& menu_style = context.theme.menu_text_style();
    const auto&      era        = detail::controls(context.theme).era_menu;
    header_padding_             = era ? era->bar_item_padding : MenuHeaderPadding;
    header_margin_              = era ? Thickness{} : MenuHeaderMargin;
    float line_height           = context.measurer.measure("", menu_style).height;

    header_widths_.clear();
    header_widths_.reserve(items_.size());
    float total_width = 0.0f;
    for (const auto& item : items_)
    {
        const Size s  = context.measurer.measure(item.text, menu_style);
        line_height   = std::max(line_height, s.height);
        const float w = s.width + header_padding_.horizontal() + header_margin_.horizontal();
        header_widths_.push_back(w);
        total_width += w;
    }
    // The minimum height is a floor: a larger font still grows the bar. An era's bar is as tall as its items.
    header_height_ = std::max(line_height + header_padding_.vertical() + header_margin_.vertical(),
                              era ? 0.0f : BarMinHeight);

    Size desired{total_width, header_height_};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

Rect MenuBarControl::header_bounds(std::size_t index) const
{
    float x = bounds().left();
    for (std::size_t i = 0; i < index && i < header_widths_.size(); ++i)
    {
        x += header_widths_[i];
    }
    const float w = index < header_widths_.size() ? header_widths_[index] : 0.0f;
    return Rect{x, bounds().top(), w, header_height_};
}

Rect MenuBarControl::header_visual(std::size_t index) const
{
    return header_bounds(index).inset(header_margin_);
}

std::size_t MenuBarControl::hit_test_header(Point p) const
{
    if (p.y < bounds().top() || p.y >= bounds().top() + header_height_)
    {
        return detail::NoIndex;
    }
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (header_bounds(i).contains(p))
        {
            return i;
        }
    }
    return detail::NoIndex;
}

ButtonState MenuBarControl::header_state(std::size_t index) const
{
    // A header's state: pressed, then hover, then open (its menu shown).
    if (!is_enabled())
    {
        return ButtonState::Disabled;
    }
    if (pointer_down_header_ == index && hovered_header_ == index)
    {
        return ButtonState::Pressed;
    }
    if (hovered_header_ == index && open_index_ != index)
    {
        return ButtonState::Hover;
    }
    if (open_index_ == index)
    {
        return ButtonState::Pressed; // an open header takes the pressed fill
    }
    return ButtonState::Normal;
}

bool MenuBarControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
        {
            const std::size_t over = hit_test_header(e.position);
            if (over != hovered_header_)
            {
                mark_dirty(DirtyFlags::Visual);
            }
            hovered_header_ = over;
            // With a menu open, the header under the pointer opens its own.
            if (over != detail::NoIndex && open_index_ != detail::NoIndex && over != open_index_)
            {
                open(over);
            }
            break;
        }
        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                pointer_down_header_ = hit_test_header(e.position);
                handled              = pointer_down_header_ != detail::NoIndex;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                const std::size_t released_on = hit_test_header(e.position);
                handled                       = pointer_down_header_ != detail::NoIndex;
                if (pointer_down_header_ != detail::NoIndex && pointer_down_header_ == released_on)
                {
                    focused_header_ = released_on;
                    if (open_index_ == released_on)
                    {
                        close();
                    }
                    else
                    {
                        open(released_on);
                    }
                }
                pointer_down_header_ = detail::NoIndex;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Leave:
            hovered_header_      = detail::NoIndex;
            pointer_down_header_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            break;
        case PointerAction::Wheel:
            break;
    }
    return handled;
}

void MenuBarControl::step_menu(int step)
{
    if (items_.empty())
    {
        return;
    }
    const auto n    = static_cast<long long>(items_.size());
    const auto from = static_cast<long long>(open_index_ != detail::NoIndex ? open_index_ : focused_header_);
    const auto next = static_cast<std::size_t>(((from + step) % n + n) % n);
    if (open_index_ != detail::NoIndex)
    {
        open(next, true);
    }
    else
    {
        focused_header_ = next;
        mark_dirty(DirtyFlags::Visual);
    }
}

bool MenuBarControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || items_.empty())
    {
        return handled;
    }
    if (open_index_ != detail::NoIndex && presenter_ && presenter_->is_open())
    {
        if (presenter_->route_key(e))
        {
            return handled; // may have invoked an item and closed the menu: nothing else to do
        }
        // A Left or Right the menu does not use moves to the next menu.
        if (e.action == KeyAction::Down && (e.key == Key::ArrowLeft || e.key == Key::ArrowRight))
        {
            step_menu(e.key == Key::ArrowRight ? 1 : -1);
        }
        // Window has already closed an open submenu on Escape and does not pass that one on.
        else if (e.action == KeyAction::Down && e.key == Key::Escape)
        {
            close();
        }
        return handled;
    }
    if (e.action != KeyAction::Down)
    {
        return handled;
    }
    switch (e.key)
    {
        case Key::ArrowLeft:
        case Key::ArrowRight:
            step_menu(e.key == Key::ArrowRight ? 1 : -1);
            break;
        case Key::ArrowDown:
        case Key::Enter:
        case Key::Space:
            open(std::min(focused_header_, items_.size() - 1), true);
            break;
        case Key::Escape:
            close();
            if (Window* window = this->window())
            {
                (void)window->end_menu_access();
            }
            break;
        default:
            break;
    }
    return handled;
}

bool MenuBarControl::handle_global_shortcut(const KeyEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    for (MenuBarItem& menu : items_)
    {
        if (MenuItem* item = find_menu_shortcut(menu.items, e))
        {
            invoke_menu_item(menu.items, *item, [this] { close(); });
            return true;
        }
    }
    return false;
}

void MenuBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (const auto& era = detail::controls(theme).era_menu)
    {
        // The Aero menu bar: the strip's gradient, a hovered item only outlined, an open one
        // pressed in. No focus ring: the keyboard's item shows as open, as in a Win32 bar.
        renderer.fill_rounded_rect_gradient(Rect{bounds().x, bounds().y, bounds().width, header_height_},
                                            0.0f,
                                            LinearGradientBrush{era->bar_background, GradientAxis::Vertical});
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            const Rect        visual = header_visual(i);
            const ButtonState state  = header_state(i);
            const Rect        bg     = visual.inset(Thickness{1.0f});
            if (state == ButtonState::Hover)
            {
                renderer.stroke_rounded_rect(visual.inset(Thickness{0.5f}), 2.0f, era->bar_hover_light, 1.0f);
                renderer.stroke_rounded_rect(bg.inset(Thickness{0.5f}), 1.0f, era->bar_hover_stroke, 1.0f);
                renderer.stroke_rounded_rect(bg.inset(Thickness{1.5f}), 0.0f, era->bar_hover_light, 1.0f);
            }
            else if (state == ButtonState::Pressed || (is_focused() && focus_visible() && i == focused_header_))
            {
                renderer.fill_rounded_rect_gradient(bg, 1.0f, LinearGradientBrush{era->bar_open_fill, GradientAxis::Vertical});
                renderer.stroke_rounded_rect(bg.inset(Thickness{0.5f}), 1.0f, era->bar_open_stroke, 1.0f);
                renderer.stroke_rounded_rect(bg.inset(Thickness{1.5f}), 0.0f, era->bar_open_inner, 1.0f);
            }
            const Rect text_rect{visual.left() + header_padding_.left,
                                 visual.top(),
                                 std::max(0.0f, visual.width - header_padding_.horizontal()),
                                 visual.height};
            renderer.draw_text(items_[i].text,
                               text_rect,
                               is_enabled() ? theme.surfaces.menu_text : era->disabled_text,
                               theme.menu_text_style());
        }
        return;
    }

    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        // The margin gap belongs to neither header's background, but the whole slot
        // (header_bounds()) still takes the click.
        const Rect visual_rect = header_visual(i);
        // A header's background follows the subtle button's ramp.
        const ButtonVisual& visual = theme.subtle_for_state(header_state(i));
        renderer.fill_rounded_rect(visual_rect, theme.corner_radius, visual.background);
        const Rect text_rect{visual_rect.left() + header_padding_.left,
                             visual_rect.top(),
                             std::max(0.0f, visual_rect.width - header_padding_.horizontal()),
                             visual_rect.height};
        renderer.draw_text(items_[i].text, text_rect, visual.text, theme.menu_text_style());
    }
}

std::unique_ptr<ui::Element> MenuBar::build() &&
{
    auto node = std::make_unique<ui::MenuBarControl>();
    apply(items, *node, [raw = node.get()](const std::vector<MenuBarItem>& v) { raw->set_items(v); });

    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

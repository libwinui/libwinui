// SPDX-License-Identifier: MIT
#include "winui/ui/menus/CommandBar.hpp"

#include "../../render/Compositor.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// CommandBar metrics.
constexpr float CommandBarCompactHeight = 48.0f; // the compact bar's height
constexpr float MinimalHeight           = 24.0f; // the minimal bar's height
constexpr float CommandBarMoreWidth     = 48.0f; // the More button's width
constexpr Thickness CommandBarMorePlate = Edges{.left = 2.0f, .top = 6.0f, .right = 6.0f, .bottom = 6.0f}; // the ellipsis glyph's margin
constexpr float    CommandBarMoreGlyphSize = 20.0f;
constexpr char32_t CommandBarMoreGlyph     = 0xE712; // More
constexpr float    ContentPadding          = 4.0f;   // the bar's 4,0,0,0 padding
// The overflow menu.
constexpr float  OverflowMinWidth = 160.0f; // mouse and keyboard; 240 for touch
constexpr float  OverflowMaxWidth = 480.0f; // the overflow's maximum width
constexpr float  OverflowItemsPad = 4.0f;   // the item list's 0,4,0,4
constexpr double OpenMs           = 250.0;  // the normal animation length
constexpr double CloseMs          = 167.0;  // the fast animation length

// The commands a point is tested against, topmost first: the last drawn wins.
void hit_order(const std::vector<Element*>& commands, std::vector<Element*>& out)
{
    out.assign(commands.rbegin(), commands.rend());
}
} // namespace

// ---- the More button ----

class CommandBarMoreButton final : public ButtonControl
{
    void sync_composition_override(detail::Compositor& /*compositor*/, const Theme& /*theme*/) override
    {
    }

public:
    explicit CommandBarMoreButton(CommandBarControl& owner) : owner_(owner)
    {
        set_variant(ButtonVariant::Subtle);
        on_click([this] { owner_.toggle_from_more_button(); });
    }

    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowDown || key == Key::ArrowUp;
    }

    // "See more" / "See less", "More options" / "Less app bar".
    [[nodiscard]] std::string tool_tip() const override
    {
        return owner_.is_open() ? strings().command_bar.see_less : strings().command_bar.see_more;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return owner_.is_open() ? strings().command_bar.less_options : strings().command_bar.more_options;
    }

protected:
    [[nodiscard]] Size measure_override(Size /*available*/, const MeasureContext&) const override
    {
        return Size{CommandBarMoreWidth, CommandBarCompactHeight};
    }
    void arrange_override(const Rect& /*final_rect*/, const ArrangeContext&) override
    {
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        // The 3-tall ellipsis glyph is centered in the 2,6,6,6 plate, so the dots sit mid-bar in
        // Compact, in Minimal's 12-tall plate and in the stretched open bar.
        const Rect         plate  = bounds().inset(CommandBarMorePlate);
        const ButtonVisual visual = style_visual(theme, ButtonVariant::Subtle, visual_state());
        draw_control_chrome(renderer, plate, theme.corner_radius, visual, theme.border_width);
        draw_icon(renderer,
                  IconGlyph{CommandBarMoreGlyph, theme.icon_font_family, CommandBarMoreGlyphSize},
                  plate,
                  visual.text,
                  CommandBarMoreGlyphSize,
                  theme.icon_font_family);
    }


private:
    CommandBarControl& owner_;

protected:
    bool key_override(const KeyEvent& e) override
    {

        const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
        if (!is_focused())
        {
            return false; // a key bubbling up from inside: this control reads only its own
        }
        // On the More button, Down or Up opens the overflow and goes into it.
        if (e.action == KeyAction::Down && (e.key == Key::ArrowDown || e.key == Key::ArrowUp) && !e.modifiers.alt &&
            is_enabled())
        {
            if (!owner_.is_open())
            {
                owner_.toggle_from_more_button();
            }
            return handled;
        }
        // The button's own handling: handle_key() would come back here, without end
        ButtonControl::key_override(e);
        return handled;
    }

protected:
    /// Around the plate inside the button.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return bounds().inset(CommandBarMorePlate);
    }
};

// ---- the open bar, lifted over the tree ----

// An open inline bar draws over its siblings, with a light-dismiss layer under it. The open bar
// is its own popup, placed by place_lift().
class CommandBarLift final : public Element
{
public:
    explicit CommandBarLift(CommandBarControl& owner) : owner_(owner)
    {
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        const std::vector<Element*> row = owner_.row();
        out.insert(out.end(), row.begin(), row.end());
    }

    // The open bar floats itself while the bar is lifted, with no shadow of its own. An outside
    // click closes the bar even with no overflow open.
    void sync()
    {
        if (!owner_.lifted())
        {
            close_popup();
            return;
        }
        open_popup({
            .content       = this,
            .light_dismiss = !owner_.sticky_,
            .chrome        = {.shadow = false},
            .place =
                [this](Element& /*content*/, Size client, const MeasureContext& context)
            {
                const Rect rect = owner_.place_lift(client, context.measurer, context.theme);
                (void)measure(Size{rect.width, rect.height}, context);
                arrange(rect);
                return true;
            },
            .on_dismiss = [this](PopupCloseReason reason) { owner_.close(reason); },
        });
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        return Size{std::isfinite(available.width) ? available.width : 0.0f,
                    std::isfinite(available.height) ? available.height : 0.0f};
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        owner_.render_row(renderer, theme, bounds());
    }
    void hit_test_children(Point /*p*/, std::vector<Element*>& out) const override
    {
        hit_order(owner_.row(), out);
    }


private:
    CommandBarControl& owner_;
};

// ---- the overflow popup ----

class CommandBarOverflow final : public Element
{
public:
    explicit CommandBarOverflow(CommandBarControl& owner) : owner_(owner)
    {
    }

    void collect_focusables(std::vector<Element*>& out) override
    {
        for (Element* item : owner_.overflow_items())
        {
            item->collect_focusables(out);
        }
    }

    void collect_live(std::vector<Element*>& out) override
    {
        for (Element* item : owner_.overflow_items())
        {
            detail::TreeAccess::collect_live(*item, out);
        }
    }

    void automation_children(std::vector<Element*>& out) const override
    {
        const std::vector<Element*> items = owner_.overflow_items();
        out.insert(out.end(), items.begin(), items.end());
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        items_ = owner_.overflow_items();
        heights_.clear();
        float width  = OverflowMinWidth;
        float height = 0.0f;
        for (Element* item : items_)
        {
            const Size s = item->measure(Size{OverflowMaxWidth, Unbounded}, context);
            width        = std::max(width, s.width);
            height += s.height;
            heights_.push_back(s.height);
        }
        border_ = context.theme.border_width;
        width   = std::min(width + 2.0f * border_, OverflowMaxWidth);
        height += 2.0f * OverflowItemsPad + 2.0f * border_;
        height = std::min(height, available.height);
        return Size{width, height};
    }

    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        float y = final_rect.y + border_ + OverflowItemsPad;
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            Element* const item = items_[i];
            const float    h    = heights_[i];
            item->arrange(Rect{final_rect.x + border_, y, std::max(0.0f, final_rect.width - 2.0f * border_), h});
            y += h;
        }
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        // The overflow slides out from under the open bar on open, and back under it on close:
        // down when the bar opens down, up when it opens up.
        const float slide  = owner_.overflow_slide();
        const float offset = (owner_.opens_up_ ? 1.0f : -1.0f) * slide * bounds().height;
        renderer.push_clip(bounds());
        if (offset != 0.0f)
        {
            renderer.push_transform(Matrix::translation(0.0f, offset));
        }
        const float radius = theme.overlay_corner_radius;
        fill_overlay_surface(renderer, theme, bounds(), radius, theme.colors().overlay);
        renderer.push_clip(bounds().inset(Thickness{border_}));
        for (Element* item : items_)
        {
            item->render(renderer, context);
        }
        renderer.pop_clip();
        stroke_rounded_rect_inside(renderer,
                                   bounds(),
                                   radius,
                                   theme.surfaces.menu_border.value_or(theme.surfaces.field_border),
                                   theme.border_width);
        if (offset != 0.0f)
        {
            renderer.pop_transform();
        }
        renderer.pop_clip();
    }

    void hit_test_children(Point /*p*/, std::vector<Element*>& out) const override
    {
        hit_order(owner_.overflow_items(), out);
    }


private:
    CommandBarControl&            owner_;
    mutable std::vector<Element*> items_;
    mutable std::vector<float>    heights_;
    mutable float                 border_ = 1.0f;
};

// ---- CommandBar ----

CommandBarControl::CommandBarControl()
{
    set_vertical_alignment(Alignment::Leading); // a bar sits at the top of its slot
    more_ = std::make_unique<CommandBarMoreButton>(*this);
    set_parent_of(*more_, this);
    auto separator = std::make_unique<AppBarSeparatorControl>();
    set_parent_of(*separator, this);
    separator_ = std::move(separator);
    lift_      = std::make_unique<CommandBarLift>(*this);
    set_parent_of(*lift_, this);
}

CommandBarControl::~CommandBarControl() = default;

void CommandBarControl::insert_primary_element(std::size_t index, std::unique_ptr<Element> command)
{
    set_parent_of(*command, this);
    if (auto* element = command_bar_element(command.get()))
    {
        element->set_command_host(this);
    }
    index = std::min(index, primary_.size());
    primary_.insert(primary_.begin() + static_cast<std::ptrdiff_t>(index), std::move(command));
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CommandBarControl::insert_secondary_element(std::size_t index, std::unique_ptr<Element> command)
{
    set_parent_of(*command, this);
    if (auto* element = command_bar_element(command.get()))
    {
        element->set_command_host(this);
    }
    index = std::min(index, secondary_.size());
    secondary_.insert(secondary_.begin() + static_cast<std::ptrdiff_t>(index), std::move(command));
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CommandBarControl::remove_primary(std::size_t index)
{
    if (index < primary_.size())
    {
        primary_.erase(primary_.begin() + static_cast<std::ptrdiff_t>(index));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CommandBarControl::remove_secondary(std::size_t index)
{
    if (index < secondary_.size())
    {
        secondary_.erase(secondary_.begin() + static_cast<std::ptrdiff_t>(index));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CommandBarControl::set_content_element(std::unique_ptr<Element> content)
{
    if (content)
    {
        set_parent_of(*content, this);
    }
    content_ = std::move(content);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CommandBarControl::set_default_text_position(CommandBarDefaultTextPosition position)
{
    text_position_ = position;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CommandBarControl::enabled_changed()
{
    static_cast<CommandBarMoreButton&>(*more_).set_enabled(is_enabled());
    if (!is_enabled())
    {
        set_open(false);
    }
    mark_dirty(DirtyFlags::Visual);
}

void CommandBarControl::set_open(bool open)
{
    if (!open)
    {
        close(PopupCloseReason::Programmatic);
        return;
    }
    if (open_)
    {
        return;
    }
    open_ = true;
    if (!open_)
    {
        focus_overflow_ = false;
    }
    else if (phase_ == Phase::Closed)
    {
        direction_known_ = false; // a new opening decides its direction afresh
    }
    phase_    = open_ ? Phase::Opening : Phase::Closing;
    phase_ms_ = 0.0;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    sync_popup();
    // on_opening fires as the bar opens and on_closing before it closes (it can cancel).
    // on_opened and on_closed fire once the transition has run.
    if (open_ && on_opening_)
    {
        on_opening_();
    }
}

void CommandBarControl::close(PopupCloseReason reason)
{
    if (!open_)
    {
        return;
    }
    if (on_closing_)
    {
        PopupClosingArgs args{.reason = reason};
        bool             canceled = false;
        canceled                  = detail::canceled(on_closing_(args));
        if (canceled)
        {
            return;
        }
    }
    close_reason_   = reason;
    open_           = false;
    focus_overflow_ = false;
    phase_          = Phase::Closing;
    phase_ms_       = 0.0;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    sync_popup();
}

void CommandBarControl::command_invoked(CommandBarElement& /*command*/)
{
    close(PopupCloseReason::Invoked);
}

void CommandBarControl::toggle_from_more_button()
{
    if (open_)
    {
        close(PopupCloseReason::CloseButton);
        return;
    }
    focus_overflow_ = true;
    set_open(true);
}

bool CommandBarControl::more_button_visible() const
{
    switch (more_visibility_)
    {
        case AutoVisibility::Visible:
            return true;
        case AutoVisibility::Collapsed:
            return false;
        case AutoVisibility::Auto:
            break;
    }
    // The More button shows for anything in the overflow, a closed mode that hides the commands,
    // or a Bottom label that opening would reveal.
    if (!secondary_.empty() || std::find(moved_.begin(), moved_.end(), true) != moved_.end())
    {
        return true;
    }
    if (closed_mode_ != AppBarClosedDisplayMode::Compact)
    {
        return true;
    }
    if (text_position_ == CommandBarDefaultTextPosition::Bottom)
    {
        for (const auto& command : primary_)
        {
            const auto* element = command_bar_element(command.get());
            const auto* button  = dynamic_cast<const AppBarButtonControl*>(element);
            if (element != nullptr && element->has_text() && command->visibility() == Visibility::Visible &&
                (button == nullptr || button->text_position() != CommandBarTextPosition::Collapsed))
            {
                return true;
            }
        }
    }
    return false;
}

std::vector<Element*> CommandBarControl::overflow_items() const
{
    std::vector<Element*> items;
    bool                  any_moved = false;
    for (std::size_t i = 0; i < primary_.size(); ++i)
    {
        const auto* element = command_bar_element(primary_[i].get());
        if (is_primary_in_overflow(i) && primary_[i]->visibility() == Visibility::Visible &&
            (element == nullptr || !element->is_separator()))
        {
            items.push_back(primary_[i].get());
            any_moved = true;
        }
    }
    bool any_secondary = false;
    for (const auto& command : secondary_)
    {
        any_secondary = any_secondary || command->visibility() == Visibility::Visible;
    }
    if (any_moved && any_secondary)
    {
        items.push_back(separator_.get()); // CommandBar's own separator before the secondaries
    }
    for (const auto& command : secondary_)
    {
        if (command->visibility() == Visibility::Visible)
        {
            items.push_back(command.get());
        }
    }
    return items;
}

void CommandBarControl::for_each_child(ChildVisitor visit) const
{
    if (shows_commands())
    {
        if (content_)
        {
            visit(*content_);
        }
        for (std::size_t i = 0; i < primary_.size(); ++i)
        {
            if (!is_primary_in_overflow(i) && primary_[i]->visibility() == Visibility::Visible)
            {
                visit(*primary_[i]);
            }
        }
    }
    if (shows_more_button())
    {
        visit(*more_);
    }
}

std::vector<Element*> CommandBarControl::row() const
{
    std::vector<Element*> out;
    for_each_child([&out](Element& element) { out.push_back(&element); });
    return out;
}

void CommandBarControl::update_looks(bool compact) const
{
    // What the overflow's rows make room for (CommandBar's flags over its secondary items).
    AppBarLook overflow{};
    overflow.overflow                 = true;
    overflow.text_position            = text_position_;
    const std::vector<Element*> items = overflow_items();
    for (Element* item : items)
    {
        if (const auto* element = command_bar_element(item))
        {
            overflow.overflow_icons   = overflow.overflow_icons || element->has_icon();
            overflow.overflow_toggles = overflow.overflow_toggles || element->is_toggle();
        }
    }
    for (Element* item : items)
    {
        if (auto* element = command_bar_element(item))
        {
            element->set_app_bar_look(overflow);
        }
    }

    AppBarLook bar{};
    bar.compact       = compact;
    bar.text_position = text_position_;
    for (std::size_t i = 0; i < primary_.size(); ++i)
    {
        if (!is_primary_in_overflow(i))
        {
            if (auto* element = command_bar_element(primary_[i].get()))
            {
                element->set_app_bar_look(bar);
            }
        }
    }
}

Size CommandBarControl::measure_override(Size available, const MeasureContext& context) const
{
    // The primaries' labels show only while the bar is open.
    const bool compact = !open_;
    moved_.assign(primary_.size(), false);
    update_looks(compact);

    // The overflow's shortcut column is as wide as its widest shortcut.
    auto measure_primaries = [&]
    {
        primary_size_.assign(primary_.size(), Size{});
        float height = CommandBarCompactHeight;
        for (std::size_t i = 0; i < primary_.size(); ++i)
        {
            if (primary_[i]->visibility() != Visibility::Visible)
            {
                continue;
            }
            const Size s     = primary_[i]->measure(Size{Unbounded, Unbounded}, context);
            primary_size_[i] = s;
            height           = std::max(height, s.height);
        }
        return height;
    };
    float height   = measure_primaries();
    content_width_ = 0.0f;
    if (content_)
    {
        const Size s   = content_->measure(Size{Unbounded, Unbounded}, context);
        content_size_  = s;
        content_width_ = s.width + ContentPadding;
        height         = std::max(height, s.height);
    }

    auto used_width = [&]
    {
        float width = content_width_;
        for (std::size_t i = 0; i < primary_.size(); ++i)
        {
            if (!moved_[i])
            {
                width += primary_size_[i].width;
            }
        }
        return width + (more_button_visible() ? CommandBarMoreWidth : 0.0f);
    };

    // Dynamic overflow: dynamic_overflow_order() groups first, lowest first, then from the right,
    // one at a time.
    if (dynamic_overflow_ && std::isfinite(available.width) && used_width() > available.width)
    {
        auto is_separator = [&](std::size_t i)
        {
            const auto* element = command_bar_element(primary_[i].get());
            return element != nullptr && element->is_separator();
        };
        auto move = [&](std::size_t i)
        {
            moved_[i] = true;
            // A separator beside a moved command goes with it (and is dropped).
            if (i > 0 && is_separator(i - 1))
            {
                moved_[i - 1] = true;
            }
            if (i + 1 < primary_.size() && is_separator(i + 1))
            {
                moved_[i + 1] = true;
            }
        };
        std::set<int> orders;
        for (const auto& command : primary_)
        {
            if (const auto* element = command_bar_element(command.get());
                element != nullptr && element->dynamic_overflow_order() > 0)
            {
                orders.insert(element->dynamic_overflow_order());
            }
        }
        for (int order : orders)
        {
            if (used_width() <= available.width)
            {
                break;
            }
            for (std::size_t i = 0; i < primary_.size(); ++i)
            {
                const auto* element = command_bar_element(primary_[i].get());
                if (element != nullptr && element->dynamic_overflow_order() == order && !is_separator(i))
                {
                    move(i);
                }
            }
        }
        for (std::size_t i = primary_.size(); i-- > 0 && used_width() > available.width;)
        {
            if (!moved_[i] && !is_separator(i) && primary_[i]->visibility() == Visibility::Visible)
            {
                move(i);
            }
        }
        // A separator left at either end of the row separates nothing.
        for (std::size_t i = primary_.size(); i-- > 0;)
        {
            if (moved_[i] || primary_[i]->visibility() != Visibility::Visible)
            {
                continue;
            }
            if (is_separator(i))
            {
                moved_[i] = true;
            }
            break;
        }
        update_looks(compact);
        height = std::max(measure_primaries(), content_ ? content_size_.height : 0.0f);
    }

    // The open bar's height is the row with labels revealed. The layout height is
    // the closed one: 48 for Compact unless a command is taller, 24, or 0.
    open_height_ = height;
    if (!compact)
    {
        update_looks(true);
        const float compact_height = std::max(measure_primaries(), content_ ? content_size_.height : 0.0f);
        update_looks(false);
        (void)measure_primaries(); // the open looks are the ones arranged and drawn
        height = compact_height;
    }
    switch (closed_mode_)
    {
        case AppBarClosedDisplayMode::Compact:
            closed_height_ = height;
            break;
        case AppBarClosedDisplayMode::Minimal:
            closed_height_ = MinimalHeight;
            break;
        case AppBarClosedDisplayMode::Hidden:
            closed_height_ = 0.0f;
            break;
    }

    // The overflow's rows, measured now so their looks are settled before the bar is drawn.
    float shortcut_width = 0.0f;
    for (Element* item : overflow_items())
    {
        if (const auto* element = command_bar_element(item))
        {
            const std::string text = element->shortcut_text();
            if (!text.empty())
            {
                shortcut_width = std::max(shortcut_width, context.measurer.measure(text, context.theme.type.caption).width);
            }
        }
    }
    for (Element* item : overflow_items())
    {
        if (auto* element = command_bar_element(item))
        {
            AppBarLook look              = element->app_bar_look();
            look.overflow_shortcut_width = shortcut_width;
            element->set_app_bar_look(look);
        }
    }

    (void)more_->measure(Size{Unbounded, Unbounded}, context);
    const float width = std::isfinite(available.width) ? available.width : used_width();
    return Size{width, closed_height_};
}

void CommandBarControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    // The closed geometry. While the bar is lifted, place_lift() arranges the row again in the
    // open bar's rect, after this and before anything draws or is hit.
    arrange_row(Rect{final_rect.x, final_rect.y, final_rect.width, closed_height_});
    sync_popup(); // which commands overflow follows the layout
}

void CommandBarControl::arrange_row(const Rect& rect)
{
    // Content on the left after the 4 DIP padding, the primaries against the More button at the
    // right, the More button as tall as the bar.
    if (content_)
    {
        const Size s = content_size_;
        content_->arrange(Rect{rect.x + ContentPadding, rect.y, s.width, s.height});
    }
    const bool more = more_button_visible();
    float      x    = rect.right() - (more ? CommandBarMoreWidth : 0.0f);
    if (more)
    {
        more_->arrange(Rect{x, rect.y, CommandBarMoreWidth, rect.height});
    }
    else
    {
        more_->arrange(Rect{rect.right(), rect.y, 0.0f, 0.0f});
    }
    for (std::size_t i = primary_.size(); i-- > 0;)
    {
        if (is_primary_in_overflow(i) || primary_[i]->visibility() != Visibility::Visible)
        {
            continue;
        }
        const Size s = i < primary_size_.size() ? primary_size_[i] : Size{};
        x -= s.width;
        primary_[i]->arrange(Rect{x, rect.y, s.width, s.height});
    }
}

Rect CommandBarControl::place_lift(Size client, const TextMeasurer& measurer, const Theme& theme)
{
    const Rect slot = bounds();
    if (!direction_known_)
    {
        // Opens up when the open bar, or the overflow under it, has no room below. Checked once
        // as it opens.
        float overflow_height = 0.0f;
        if (!overflow_items().empty())
        {
            overflow_height = overflow_menu().measure(Size{Unbounded, Unbounded}, measurer, theme).height;
        }
        const float bottom = slot.y + open_height_;
        opens_up_          = bottom > client.height + 1.0f || bottom + overflow_height > client.height;
        direction_known_   = true;
    }
    // Down from the closed bar's top, or up from its bottom.
    open_rect_ = opens_up_ ? Rect{slot.x, slot.y + slot.height - open_height_, slot.width, open_height_}
                           : Rect{slot.x, slot.y, slot.width, open_height_};
    arrange_row(open_rect_);
    return open_rect_;
}

void CommandBarControl::render_row(Renderer& renderer, const Theme& theme, const Rect& rect) const
{
    const bool lifted_look = lifted();
    if (lifted_look)
    {
        renderer.push_clip(rect); // clipped to the open bar's own rect
    }
    if (const std::optional<Color> fill = background_.resolve(theme))
    {
        renderer.fill_rounded_rect(rect, lifted_look ? theme.corner_radius : 0.0f, *fill);
    }
    // Open from Compact: the in-app acrylic inside a 1px card stroke frame at the control
    // radius. Open from Minimal or Hidden: neither.
    if (lifted_look && closed_mode_ == AppBarClosedDisplayMode::Compact)
    {
        fill_overlay_surface(renderer, theme, rect, theme.corner_radius, theme.colors().overlay);
        const Color frame = detail::controls(theme).card_surfaces
                                ? theme.palette.card_stroke_default
                                : theme.surfaces.menu_border.value_or(theme.surfaces.field_border);
        stroke_rounded_rect_inside(renderer, rect, theme.corner_radius, frame, theme.border_width);
    }
    for (Element* element : row())
    {
        element->render(renderer, theme);
    }
    if (lifted_look)
    {
        renderer.pop_clip();
    }
}

void CommandBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (lifted())
    {
        return; // the lift draws the open bar, over the tree
    }
    render_row(renderer, theme, bounds());
}

void CommandBarControl::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    // Lifted, the row is in the lift, which is hit-tested on its own.
    if (!lifted())
    {
        hit_order(row(), out);
    }
}

void CommandBarControl::collect_live(std::vector<Element*>& out)
{
    // The open bar first, so the overflow (and a command's flyout) float over it.
    out.push_back(lift_.get());
    out.push_back(this);
    for (Element* element : row())
    {
        if (element == more_.get())
        {
            out.push_back(element); // its tooltip
        }
        else
        {
            detail::TreeAccess::collect_live(*element, out);
        }
    }
}

bool CommandBarControl::handle_global_shortcut(const KeyEvent& e)
{
    // CommandBar registers its secondary commands' shortcuts while they are hidden.
    for (Element* item : overflow_items())
    {
        if (item->handle_global_shortcut(e))
        {
            return true;
        }
    }
    return false;
}

void CommandBarControl::clear_dirty_override(DirtyFlags flags)
{
    // Every owned child, not only the row for_each_child() lists.
    if (content_)
    {
        clear_dirty_of(*content_, flags);
    }
    for (const auto& command : primary_)
    {
        clear_dirty_of(*command, flags);
    }
    for (const auto& command : secondary_)
    {
        clear_dirty_of(*command, flags);
    }
    clear_dirty_of(*more_, flags);
    clear_dirty_of(*separator_, flags);
    clear_dirty_of(*lift_, flags);
    if (overflow_)
    {
        clear_dirty_of(*overflow_, flags);
    }
}

void CommandBarControl::tick_override(Duration delta)
{
    // The parts for_each_child() lists are ticked as children; the rest (the overflow commands,
    // a hidden row) are ticked here.
    std::vector<const Element*> listed;
    for_each_child([&listed](Element& child) { listed.push_back(&child); });
    const auto tick_unlisted = [&listed, delta](Element* part)
    {
        if (part != nullptr && std::ranges::find(listed, part) == listed.end())
        {
            part->tick(delta);
        }
    };
    tick_unlisted(content_.get());
    for (const auto& command : primary_)
    {
        tick_unlisted(command.get());
    }
    for (const auto& command : secondary_)
    {
        tick_unlisted(command.get());
    }
    tick_unlisted(more_.get());
    if (phase_ == Phase::Opening || phase_ == Phase::Closing)
    {
        const bool   opening = phase_ == Phase::Opening;
        const double length  = opening ? OpenMs : CloseMs;
        phase_ms_ += animation_delta(delta).count();
        mark_dirty(DirtyFlags::Visual);
        if (phase_ms_ >= length)
        {
            phase_    = opening ? Phase::Open : Phase::Closed;
            phase_ms_ = 0.0;
            if (!opening)
            {
                mark_dirty(DirtyFlags::Layout); // back to the closed geometry
                sync_popup();
            }
            if (opening && on_opened_)
            {
                on_opened_();
            }
            else if (!opening && on_closed_)
            {
                on_closed_(close_reason_);
            }
        }
    }
}

void CommandBarControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    // The listed parts are synced as children; the primary commands in the overflow are not listed.
    std::vector<const Element*> listed;
    for_each_child([&listed](Element& child) { listed.push_back(&child); });
    for (const auto& command : primary_)
    {
        if (std::ranges::find(listed, command.get()) == listed.end())
        {
            detail::TreeAccess::sync_composition(*command, compositor, theme);
        }
    }
}

void CommandBarControl::sync_native(NativeHost& host)
{
    if (content_)
    {
        content_->sync_native(host);
    }
}

Element& CommandBarControl::overflow_menu()
{
    if (!overflow_)
    {
        overflow_ = std::make_unique<CommandBarOverflow>(*this);
        set_parent_of(*overflow_, this);
    }
    return *overflow_;
}

void CommandBarControl::sync_popup()
{
    static_cast<CommandBarLift*>(lift_.get())->sync();
    // The overflow, while the bar is open (or still animating closed) and has entries.
    if (!lifted() || overflow_items().empty())
    {
        close_popup();
        return;
    }
    open_popup({
        .content       = &overflow_menu(),
        .light_dismiss = !sticky_,
        .takes_focus   = open_ && focus_overflow_, // only when opened from the More button
        .place         = [this](Element& content, Size client, const MeasureContext& context)
        { return place_overflow(content, client, context.measurer, context.theme); },
        .on_dismiss = [this](PopupCloseReason reason) { close(reason); },
    });
}

bool CommandBarControl::place_overflow(Element& content, Size client, const TextMeasurer& measurer, const Theme& theme)
{
    // Under the open bar, or above it when opening up. Right edges aligned, or left ones when
    // crossing the window edge.
    const Rect  bar       = open_rect_;
    const float available = opens_up_ ? std::max(0.0f, bar.top()) : std::max(0.0f, client.height - bar.bottom());
    const Size  desired   = content.measure(Size{client.width, available}, measurer, theme);
    const float width     = std::min(desired.width, client.width);
    const float height    = std::min(desired.height, available);
    float       x         = bar.right() - width;
    if (x < 0.0f)
    {
        x = bar.left();
    }
    x             = std::clamp(x, 0.0f, std::max(0.0f, client.width - width));
    const float y = opens_up_ ? bar.top() - height : bar.bottom();
    content.arrange(Rect{x, y, width, height});
    return true;
}

float CommandBarControl::overflow_slide() const
{
    const auto eased = [](double t) { return easing::evaluate(easing::ControlFastOutSlowIn, std::clamp(t, 0.0, 1.0)); };
    if (!animations_enabled() && (phase_ == Phase::Opening || phase_ == Phase::Closing))
    {
        return phase_ == Phase::Opening ? 0.0f : 1.0f;
    }
    switch (phase_)
    {
        case Phase::Opening:
            return 1.0f - eased(phase_ms_ / OpenMs);
        case Phase::Closing:
            return eased(phase_ms_ / CloseMs);
        case Phase::Open:
            return 0.0f;
        case Phase::Closed:
            break;
    }
    return 1.0f;
}

std::unique_ptr<ui::Element> CommandBar::build() &&
{
    auto node = std::make_unique<ui::CommandBarControl>();
    apply(default_text_position,
          *node,
          [raw = node.get()](const CommandBarDefaultTextPosition& v) { raw->set_default_text_position(v); });
    apply(overflow_button_visibility,
          *node,
          [raw = node.get()](const AutoVisibility& v) { raw->set_overflow_button_visibility(v); });
    apply(dynamic_overflow_enabled, *node, [raw = node.get()](const bool& v) { raw->set_dynamic_overflow_enabled(v); });
    apply(sticky, *node, [raw = node.get()](const bool& v) { raw->set_sticky(v); });
    apply(closed_display_mode,
          *node,
          [raw = node.get()](const AppBarClosedDisplayMode& v) { raw->set_closed_display_mode(v); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(background, *node, [raw = node.get()](const Paint& v) { raw->set_background(v); });
    if (content)
    {
        node->set_content(content.take());
    }
    for (auto& command : primary.nodes)
    {
        node->insert_primary_element(node->primary_count(), std::move(command));
    }
    for (auto& command : secondary.nodes)
    {
        node->insert_secondary_element(node->secondary_count(), std::move(command));
    }
    // A two-way `.is_open` follows the control: the user opening or closing it writes the property.
    const auto is_open_writer = ui::shared_writer(is_open);
    apply(is_open,
          *node,
          [raw = node.get(), is_open_writer](const bool& value)
          {
              raw->set_open(value);
              // on_closing canceled the close the property asked for: put the property back.
              if (raw->is_open() != value && is_open_writer)
              {
                  (*is_open_writer)(raw->is_open());
              }
          });
    if (on_opening)
    {
        node->on_opening(std::move(on_opening));
    }
    if (auto handler = ui::write_back_value<bool>(is_open_writer, true, std::move(on_opened)))
    {
        node->on_opened(std::move(handler));
    }
    if (on_closing)
    {
        node->on_closing(std::move(on_closing));
    }
    if (auto handler = ui::write_back_value<bool>(is_open_writer, false, std::move(on_closed)))
    {
        node->on_closed(std::move(handler));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

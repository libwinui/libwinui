// SPDX-License-Identifier: MIT
#include "winui/ui/menus/CommandBarFlyout.hpp"

#include "../../render/Compositor.hpp"
#include "../dialogs/FlyoutSpec.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace
{
// CommandBarFlyout metrics.
constexpr float RowInset                  = 3.0f;  // the primary row's 3,3,0,3 margin
constexpr float ButtonSize                = 40.0f; // the primary buttons, 40 square
constexpr float CommandBarFlyoutMoreWidth = 44.0f; // the More button's width
constexpr Thickness CommandBarFlyoutMorePlate = Edges{.left = 2.0f, .top = 2.0f, .right = 6.0f, .bottom = 2.0f}; // the More button's plate inset
constexpr float    CommandBarFlyoutMoreGlyphSize = 16.0f;
constexpr char32_t CommandBarFlyoutMoreGlyph     = 0xE712;
constexpr float    MenuInset                     = 3.0f;   // the secondary item list's margin
constexpr float    MenuMinWidth                  = 136.0f; // the overflow menu's minimum width
constexpr float    CommandBarFlyoutMaxWidth      = 440.0f; // the command bar's maximum width
constexpr double   CommandBarFlyoutExpandMs      = 250.0;  // collapsed to expanded
constexpr double   CommandBarFlyoutCollapseMs    = 167.0;  // expanded to collapsed
constexpr double   FadeMs                        = 83.0;   // the fade in and out

AppBarLook primary_look()
{
    AppBarLook look;
    look.flyout_primary = true;
    look.text_position  = CommandBarDefaultTextPosition::Collapsed;
    return look;
}
} // namespace

// ---- the More button ----

class CommandBarFlyoutMoreButton final : public ButtonControl
{
    void sync_composition_override(detail::Compositor& /*compositor*/, const Theme& /*theme*/) override
    {
    }

public:
    explicit CommandBarFlyoutMoreButton(CommandBarFlyoutControl& owner) : owner_(owner)
    {
        set_variant(ButtonVariant::Subtle);
        on_click(
            [this]
            {
                // The command bar opening switches the flyout to Standard.
                owner_.set_show_mode(FlyoutShowMode::Standard);
                owner_.set_expanded(!owner_.is_expanded());
            });
    }
    [[nodiscard]] std::string tool_tip() const override
    {
        return owner_.is_expanded() ? strings().command_bar.see_less : strings().command_bar.see_more;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return owner_.is_expanded() ? strings().command_bar.less_options : strings().command_bar.more_options;
    }

protected:
    [[nodiscard]] Size measure_override(Size /*available*/, const MeasureContext&) const override
    {
        return Size{CommandBarFlyoutMoreWidth, ButtonSize};
    }
    void arrange_override(const Rect& /*final_rect*/, const ArrangeContext&) override
    {
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme&       theme  = context.theme;
        const Rect         plate  = bounds().inset(CommandBarFlyoutMorePlate);
        const ButtonVisual visual = style_visual(theme, ButtonVariant::Subtle, visual_state());
        draw_control_chrome(renderer, plate, theme.corner_radius, visual, theme.border_width);
        draw_icon(renderer,
                  IconGlyph{CommandBarFlyoutMoreGlyph, theme.icon_font_family, CommandBarFlyoutMoreGlyphSize},
                  plate,
                  visual.text,
                  CommandBarFlyoutMoreGlyphSize,
                  theme.icon_font_family);
    }


private:
    CommandBarFlyoutControl& owner_;

protected:
    /// Around the plate inside the button.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return bounds().inset(CommandBarFlyoutMorePlate);
    }
};

// ---- the presenter: the row, and the menu under it ----

class CommandBarFlyoutPresenter final : public Element
{
public:
    explicit CommandBarFlyoutPresenter(CommandBarFlyoutControl& owner) : owner_(owner)
    {
    }

    [[nodiscard]] bool more_visible() const
    {
        return !owner_.always_expanded_ && !owner_.secondary_.empty();
    }
    [[nodiscard]] bool menu_shown() const
    {
        return !owner_.secondary_.empty() && owner_.menu_reveal() > 0.0f;
    }

    [[nodiscard]] float visible_height() const
    {
        const float reveal = owner_.menu_reveal();
        return std::min(bounds().height, row_height_ + border_ + (menu_shown() ? (1.0f + menu_height_) * reveal : 0.0f));
    }

    [[nodiscard]] std::vector<Element*> commands(bool with_menu) const
    {
        std::vector<Element*> out;
        out.reserve(owner_.primary_.size() + 1);
        for (const auto& command : owner_.primary_)
        {
            out.push_back(command.get());
        }
        if (more_visible())
        {
            out.push_back(owner_.more_.get());
        }
        if (with_menu)
        {
            for (const auto& command : owner_.secondary_)
            {
                out.push_back(command.get());
            }
        }
        return out;
    }

    void collect_focusables(std::vector<Element*>& out) override
    {
        for (Element* command : commands(menu_shown()))
        {
            command->collect_focusables(out);
        }
    }

    void collect_live(std::vector<Element*>& out) override
    {
        for (Element* command : commands(menu_shown()))
        {
            detail::TreeAccess::collect_live(*command, out);
        }
    }

protected:
    [[nodiscard]] Size measure_override(Size /*available*/, const MeasureContext& context) const override
    {
        border_         = context.theme.border_width;
        float row_width = border_ + RowInset;
        for (const auto& command : owner_.primary_)
        {
            row_width += command->measure(Size{Unbounded, Unbounded}, context).width;
        }
        row_width += more_visible() ? CommandBarFlyoutMoreWidth : RowInset;
        (void)owner_.more_->measure(Size{Unbounded, Unbounded}, context);
        collapsed_width_ = row_width + border_;
        row_height_      = border_ + RowInset + ButtonSize + RowInset;

        // The menu's width when expanded.
        AppBarLook menu{};
        menu.overflow        = true;
        menu.flyout_overflow = true;
        float shortcut       = 0.0f;
        for (const auto& command : owner_.secondary_)
        {
            if (const auto* element = command_bar_element(command.get()))
            {
                menu.overflow_icons    = menu.overflow_icons || element->has_icon();
                menu.overflow_toggles  = menu.overflow_toggles || element->is_toggle();
                const std::string text = element->shortcut_text();
                if (!text.empty())
                {
                    shortcut = std::max(shortcut, context.measurer.measure(text, context.theme.type.caption).width);
                }
            }
        }
        menu.overflow_shortcut_width = shortcut;
        float menu_width             = MenuMinWidth;
        menu_heights_.clear();
        menu_height_ = 0.0f;
        for (const auto& command : owner_.secondary_)
        {
            if (auto* element = command_bar_element(command.get()))
            {
                element->set_app_bar_look(menu);
            }
            const Size s = command->measure(Size{CommandBarFlyoutMaxWidth, Unbounded}, context);
            menu_width   = std::max(menu_width, s.width + 2.0f * MenuInset);
            menu_heights_.push_back(s.height);
            menu_height_ += s.height;
        }
        menu_height_ += owner_.secondary_.empty() ? 0.0f : 2.0f * MenuInset;
        expanded_width_ = std::min(CommandBarFlyoutMaxWidth, std::max(collapsed_width_, menu_width + 2.0f * border_));

        const bool shown = menu_shown();
        return Size{shown ? expanded_width_ : collapsed_width_,
                    row_height_ + (shown ? 1.0f + menu_height_ : 0.0f) + border_};
    }

    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        float x = final_rect.x + border_ + RowInset;
        for (const auto& command : owner_.primary_)
        {
            command->arrange(Rect{x, final_rect.y + border_ + RowInset, ButtonSize, ButtonSize});
            x += ButtonSize;
        }
        if (more_visible())
        {
            owner_.more_->arrange(Rect{x, final_rect.y + border_ + RowInset, CommandBarFlyoutMoreWidth, ButtonSize});
        }
        else
        {
            owner_.more_->arrange(Rect{x, final_rect.y, 0.0f, 0.0f});
        }
        float y = final_rect.y + row_height_ + 1.0f + MenuInset;
        for (std::size_t i = 0; i < owner_.secondary_.size() && i < menu_heights_.size(); ++i)
        {
            owner_.secondary_[i]->arrange(Rect{final_rect.x + border_ + MenuInset,
                                               y,
                                               std::max(0.0f, final_rect.width - 2.0f * (border_ + MenuInset)),
                                               menu_heights_[i]});
            y += menu_heights_[i];
        }
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        const Rect   surface{bounds().x, bounds().y, bounds().width, visible_height()};
        const float  radius = theme.overlay_corner_radius;
        fill_overlay_surface(renderer, theme, surface, radius, theme.colors().overlay);
        renderer.push_clip(surface);
        for (const auto& command : owner_.primary_)
        {
            command->render(renderer, context);
        }
        if (more_visible())
        {
            owner_.more_->render(renderer, context);
        }
        if (menu_shown())
        {
            renderer.fill_rounded_rect(Rect{bounds().x, bounds().y + row_height_, bounds().width, 1.0f},
                                       0.0f,
                                       detail::controls(theme).card_surfaces ? theme.palette.control_stroke_default
                                                                             : theme.surfaces.field_border);
            for (const auto& command : owner_.secondary_)
            {
                command->render(renderer, context);
            }
        }
        renderer.pop_clip();
        const Color frame = detail::controls(theme).card_surfaces
                                ? theme.palette.control_stroke_default
                                : theme.surfaces.menu_border.value_or(theme.surfaces.field_border);
        stroke_rounded_rect_inside(renderer, surface, radius, frame, theme.border_width);
    }

    void hit_test_children(Point /*p*/, std::vector<Element*>& out) const override
    {
        const auto list = commands(menu_shown() && owner_.menu_reveal() >= 1.0f);
        out.assign(list.rbegin(), list.rend());
    }

    void clear_dirty_override(DirtyFlags flags) override
    {
        for (Element* command : commands(true))
        {
            clear_dirty_of(*command, flags);
        }
    }


private:
    CommandBarFlyoutControl&   owner_;
    mutable float              border_          = 1.0f;
    mutable float              collapsed_width_ = 0.0f;
    mutable float              expanded_width_  = 0.0f;
    mutable float              row_height_      = 0.0f;
    mutable float              menu_height_     = 0.0f;
    mutable std::vector<float> menu_heights_;
};

// ---- CommandBarFlyout ----

CommandBarFlyoutControl::CommandBarFlyoutControl()
{
    set_transition_durations(Duration{FadeMs}, Duration{FadeMs});
    set_placement(PopupPlacement::Above); // a command bar opens above what it acts on
    more_ = std::make_unique<CommandBarFlyoutMoreButton>(*this);
    set_parent_of(*more_, this);
}

CommandBarFlyoutControl::~CommandBarFlyoutControl() = default;

void CommandBarFlyoutControl::add_command(std::vector<std::unique_ptr<Element>>& list, std::unique_ptr<Element> command, bool primary)
{
    set_parent_of(*command, this);
    if (auto* element = command_bar_element(command.get()))
    {
        element->set_command_host(this);
        if (primary)
        {
            element->set_app_bar_look(primary_look());
        }
    }
    list.push_back(std::move(command));
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CommandBarFlyoutControl::set_expanded(bool expanded)
{
    if (always_expanded_)
    {
        expanded = true;
    }
    if (expanded == expanded_)
    {
        return;
    }
    expanded_   = expanded;
    expanding_  = expanded;
    collapsing_ = !expanded;
    expand_ms_  = 0.0;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

float CommandBarFlyoutControl::menu_reveal() const
{
    if (expanding_)
    {
        return easing::evaluate(easing::ControlFastOutSlowIn, std::clamp(expand_ms_ / CommandBarFlyoutExpandMs, 0.0, 1.0));
    }
    if (collapsing_)
    {
        return 1.0f - easing::evaluate(easing::ControlFastOutSlowIn,
                                       std::clamp(expand_ms_ / CommandBarFlyoutCollapseMs, 0.0, 1.0));
    }
    return expanded_ ? 1.0f : 0.0f;
}

void CommandBarFlyoutControl::opening_override()
{
    // A Standard flyout (a context menu) opens with its menu shown, a Transient one (a click)
    // with it collapsed. An always-expanded flyout always shows it.
    expanded_   = always_expanded_ || (show_mode() == FlyoutShowMode::Standard && !secondary_.empty());
    expanding_  = false;
    collapsing_ = false;
    expand_ms_  = 0.0;
    for (const auto& command : primary_)
    {
        if (auto* element = command_bar_element(command.get()))
        {
            element->set_app_bar_look(primary_look());
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CommandBarFlyoutControl::tick_override(Duration delta)
{
    FlyoutBase::tick_override(delta);
    for (const auto& command : primary_)
    {
        command->tick(delta);
    }
    for (const auto& command : secondary_)
    {
        command->tick(delta);
    }
    if (expanding_ || collapsing_)
    {
        expand_ms_ += animation_delta(delta).count();
        const double length = expanding_ ? CommandBarFlyoutExpandMs : CommandBarFlyoutCollapseMs;
        if (expand_ms_ >= length)
        {
            expanding_  = false;
            collapsing_ = false;
            expand_ms_  = 0.0;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
        else
        {
            mark_dirty(DirtyFlags::Visual);
        }
    }
}

void CommandBarFlyoutControl::command_invoked(CommandBarElement& command)
{
    // CommandBarFlyout hooks its secondary commands: their clicks close the flyout. A primary
    // command's click closes the inner command bar, which collapses the menu.
    for (const auto& secondary : secondary_)
    {
        if (command_bar_element(secondary.get()) == &command)
        {
            close(PopupCloseReason::Invoked);
            return;
        }
    }
    if (!always_expanded_)
    {
        set_expanded(false);
    }
}

bool CommandBarFlyoutControl::handle_global_shortcut(const KeyEvent& e)
{
    if (FlyoutBase::handle_global_shortcut(e))
    {
        return true;
    }
    for (const auto& command : secondary_)
    {
        if (command->handle_global_shortcut(e))
        {
            return true;
        }
    }
    return false;
}

Element* CommandBarFlyoutControl::flyout_surface()
{
    if (!presenter_)
    {
        presenter_ = std::make_unique<CommandBarFlyoutPresenter>(*this);
        set_parent_of(*presenter_, this);
    }
    return presenter_.get();
}

void CommandBarFlyoutControl::flyout_chrome(PopupChrome& chrome)
{
    chrome.shadow_rect = [this](const Rect& bounds)
    {
        if (!presenter_)
        {
            return bounds;
        }
        const auto* presenter = static_cast<const CommandBarFlyoutPresenter*>(presenter_.get());
        return Rect{bounds.x, bounds.y, bounds.width, presenter->visible_height()};
    };
}

std::unique_ptr<ui::Element> CommandBarFlyout::build() &&
{
    auto node = std::make_unique<ui::CommandBarFlyoutControl>();
    apply(placement, *node, [raw = node.get()](const PopupPlacement& v) { raw->set_placement(v); });
    apply(placement_alignment,
          *node,
          [raw = node.get()](const PopupPlacementAlignment& v) { raw->set_placement_alignment(v); });
    apply(always_expanded, *node, [raw = node.get()](const bool& v) { raw->set_always_expanded(v); });
    apply(show_mode, *node, [raw = node.get()](const FlyoutShowMode& v) { raw->set_show_mode(v); });
    if (anchor)
    {
        node->set_anchor(anchor.take());
    }
    for (auto& command : primary.nodes)
    {
        node->add_primary(std::move(command));
    }
    for (auto& command : secondary.nodes)
    {
        node->add_secondary(std::move(command));
    }
    detail::apply_flyout_open(*node, is_open, light_dismiss, on_opened, on_closing, on_closed);

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

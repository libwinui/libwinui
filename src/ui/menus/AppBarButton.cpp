// SPDX-License-Identifier: MIT
#include "winui/ui/menus/AppBarButton.hpp"

#include "../../core/StringsDetail.hpp"
#include "../../render/Compositor.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

namespace
{
// AppBarButton / AppBarToggleButton metrics.
constexpr float     Width                     = 68.0f; // the button's width
constexpr float     FullMinHeight             = 64.0f; // the content's minimum height
constexpr float     AppBarButtonCompactHeight = 48.0f; // the bar's compact height
constexpr float     AppBarButtonIconSize      = 16.0f; // the icon, scaled to fit
constexpr float     IconTop                   = 16.0f; // the icon's 0,16,0,2 margin with the label below
constexpr float     IconBottom                = 2.0f;
constexpr float     LabelFontSize             = 12.0f;                                // the label's font size
constexpr Thickness LabelMargin = Edges{.left = 2.0f, .right = 2.0f, .bottom = 8.0f}; // the label's margin
constexpr Thickness PlateMargin = Symmetric{.horizontal = 2.0f, .vertical = 6.0f};    // the plate's inset
constexpr Thickness PlateCompactMargin = Edges{.left = 2.0f, .top = 6.0f, .right = 2.0f, .bottom = 22.0f}; // the plate's inset while compact
constexpr Thickness RightIconMargin = Edges{.left = 12.0f, .top = 16.0f, .bottom = 10.0f}; // the icon's margin beside the label
constexpr Thickness RightLabelMargin = Edges{.left = 8.0f, .top = 16.0f, .right = 12.0f, .bottom = 10.0f}; // the label's margin beside the icon
// Overflow.
constexpr Thickness OverflowPlateMargin = Symmetric{.horizontal = 4.0f, .vertical = 0.0f}; // the plate's inset in the overflow
constexpr Thickness OverflowLabelPadding    = Edges{.top = 5.0f, .bottom = 8.0f}; // the label's padding in the overflow
constexpr float     OverflowInset           = 12.0f; // the overflow label's and icon's 12,0,12,0 margin
constexpr float     OverflowColumn          = 38.0f; // where the label (or icon) goes past a check or icon column
constexpr float     OverflowTwoColumn       = 76.0f; // ... past both
constexpr float     AppBarButtonShortcutGap = 24.0f; // the shortcut text's 24,0,12,0 margin
constexpr float     ShortcutRight           = 12.0f;
constexpr float     OverflowChevronRight    = 16.0f; // the overflow chevron's 0,0,16,0 margin
constexpr float     OverflowChevronSize     = 12.0f; // the overflow chevron's font size
constexpr float     BarChevronSize          = 8.0f;  // the bar chevron's font size
constexpr float     BarChevronTop           = 20.0f; // the bar chevron's -23,20,12,0 margin
constexpr float     BarChevronRight         = 12.0f;
constexpr float     BarChevronLabelOnRightLeft = -7.0f; // its -7,20,12,0 margin beside a label
constexpr float     AppBarButtonCheckGlyphSize = 16.0f; // the overflow check mark
constexpr float     CheckGlyphInset            = 12.0f; // the check mark's 12,4,12,4 margin
// A CommandBarFlyout's primary buttons.
constexpr float    FlyoutPrimarySize        = 40.0f;  // a minimum width of 40, the 40px items row
constexpr float    FlyoutPrimaryPlate       = 2.0f;   // the plate's inset
constexpr char32_t AppBarButtonChevronGlyph = 0xE974; // the overflow's submenu chevron, pointing right
constexpr char32_t BarChevronGlyph = 0xE972; // ChevronDownSmall: in the bar the menu opens below, as Explorer's Sort and View
constexpr char32_t AppBarButtonCheckGlyph = 0xE73E; // the check mark

// A CommandBarFlyout's secondary rows.
constexpr Thickness FlyoutOverflowLabelPadding = Edges{.top = 6.0f, .bottom = 7.0f};
constexpr float     FlyoutOverflowColumn       = 39.0f;
constexpr float     FlyoutOverflowTwoColumn    = 67.0f;

struct OverflowColumns
{
    float     icon_x;
    float     label_x;
    Thickness padding;
};

OverflowColumns overflow_columns(const AppBarLook& look)
{
    const float     one = look.flyout_overflow ? FlyoutOverflowColumn : OverflowColumn;
    const float     two = look.flyout_overflow ? FlyoutOverflowTwoColumn : OverflowTwoColumn;
    OverflowColumns columns{OverflowInset,
                            OverflowInset,
                            look.flyout_overflow ? FlyoutOverflowLabelPadding : OverflowLabelPadding};
    if (look.overflow_toggles && look.overflow_icons)
    {
        columns.icon_x  = one;
        columns.label_x = two;
    }
    else if (look.overflow_toggles || look.overflow_icons)
    {
        columns.label_x = one;
    }
    return columns;
}

TextStyle label_style(const Theme& theme)
{
    TextStyle style = theme.text_style;
    style.font_size = LabelFontSize;
    style.wrapping  = TextWrapping::Wrap;
    return style;
}
} // namespace

CommandBarElement* command_bar_element(Element* element)
{
    if (auto* direct = dynamic_cast<CommandBarElement*>(element))
    {
        return direct;
    }
    if (auto* flyout = dynamic_cast<FlyoutBase*>(element))
    {
        return dynamic_cast<CommandBarElement*>(flyout->anchor());
    }
    return nullptr;
}

const CommandBarElement* command_bar_element(const Element* element)
{
    return command_bar_element(const_cast<Element*>(element));
}

// ---- AppBarButton ----

AppBarButtonControl::AppBarButtonControl()
{
    // An app-bar button sits at the top left of its slot.
    set_horizontal_alignment(Alignment::Leading);
    set_vertical_alignment(Alignment::Leading);
}

AppBarButtonControl::AppBarButtonControl(std::string text, IconSource icon) : AppBarButtonControl()
{
    set_text(std::move(text));
    icon_ = std::move(icon);
}

std::string AppBarButtonControl::text() const
{
    const std::string& own = ButtonControl::text();
    if (own.empty() && command() != nullptr)
    {
        return command()->current().text;
    }
    return own;
}

const IconSource& AppBarButtonControl::icon() const
{
    if (icon_.empty() && command() != nullptr)
    {
        return command()->current().icon;
    }
    return icon_;
}

KeyCombo AppBarButtonControl::shortcut() const
{
    if (shortcut_.empty() && command() != nullptr)
    {
        return command()->current().shortcut;
    }
    return shortcut_;
}

std::string AppBarButtonControl::shortcut_text() const
{
    return shortcut_text_.empty() ? shortcut().text() : shortcut_text_;
}

void AppBarButtonControl::set_app_bar_look(const AppBarLook& look)
{
    if (look_ == look)
    {
        return;
    }
    // In the overflow a row stretches across the menu.
    if (look.overflow != look_.overflow)
    {
        set_horizontal_alignment(look.overflow ? Alignment::Stretch : Alignment::Leading);
    }
    look_ = look;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

AppBarButtonControl::Mode AppBarButtonControl::mode() const
{
    // The layout mode is picked in this order: overflow, label on the right, label collapsed,
    // compact, then full size.
    if (look_.overflow)
    {
        return Mode::Overflow;
    }
    if (look_.flyout_primary)
    {
        return Mode::FlyoutPrimary;
    }
    const CommandBarDefaultTextPosition position = text_position_ == CommandBarTextPosition::Collapsed
                                                       ? CommandBarDefaultTextPosition::Collapsed
                                                       : look_.text_position;
    if (position == CommandBarDefaultTextPosition::Right)
    {
        return Mode::LabelOnRight;
    }
    if (position == CommandBarDefaultTextPosition::Collapsed)
    {
        return Mode::LabelCollapsed;
    }
    return look_.compact ? Mode::Compact : Mode::FullSize;
}

Size AppBarButtonControl::measure_override(Size available, const MeasureContext& context) const
{
    (void)available;
    (void)icon().measure(context.measurer);
    const std::string text = this->text();
    switch (mode())
    {
        case Mode::FullSize:
        {
            label_height_ = text.empty()
                                ? 0.0f
                                : context.measurer
                                      .measure_wrapped(text, label_style(context.theme), Width - LabelMargin.horizontal())
                                      .height;
            return Size{Width,
                        std::max(FullMinHeight,
                                 IconTop + AppBarButtonIconSize + IconBottom + label_height_ + LabelMargin.bottom)};
        }
        case Mode::Compact:
        case Mode::LabelCollapsed:
            return Size{Width, AppBarButtonCompactHeight};
        case Mode::LabelOnRight:
        {
            TextStyle style        = label_style(context.theme);
            style.wrapping         = TextWrapping::NoWrap;
            const Size s           = context.measurer.measure(text, style);
            label_height_          = s.height;
            const float label_part = text.empty() ? RightLabelMargin.right : RightLabelMargin.horizontal() + s.width;
            // The chevron is a column of its own after the label, starting 7 into its right margin.
            const float chevron = flyout() != nullptr ? BarChevronLabelOnRightLeft + BarChevronSize + BarChevronRight : 0.0f;
            return Size{RightIconMargin.left + AppBarButtonIconSize + label_part + chevron,
                        std::max(AppBarButtonCompactHeight, RightLabelMargin.vertical() + s.height)};
        }
        case Mode::FlyoutPrimary:
            return Size{FlyoutPrimarySize, FlyoutPrimarySize};
        case Mode::Overflow:
        {
            const Size            s       = context.measurer.measure(text, context.theme.text_style);
            const OverflowColumns columns = overflow_columns(look_);
            float                 width   = columns.label_x + s.width + OverflowInset;
            if (look_.overflow_shortcut_width > 0.0f)
            {
                width += AppBarButtonShortcutGap + look_.overflow_shortcut_width + ShortcutRight - OverflowInset;
            }
            if (flyout() != nullptr)
            {
                width += OverflowChevronSize + OverflowChevronRight;
            }
            return Size{width, s.height + columns.padding.vertical()};
        }
    }
    return Size{};
}

void AppBarButtonControl::arrange_override(const Rect& /*final_rect*/, const ArrangeContext&)
{
    // No content to place: the icon and label are drawn from bounds() (render_override()).
}

Rect AppBarButtonControl::plate_rect() const
{
    switch (mode())
    {
        case Mode::Overflow:
            return bounds().inset(OverflowPlateMargin);
        case Mode::Compact:
        {
            // The plate of a 64-tall button inset 2,6,2,22: its top 36 of the 48 the bar shows.
            return Rect{bounds().x + PlateCompactMargin.left,
                        bounds().y + PlateCompactMargin.top,
                        std::max(0.0f, bounds().width - PlateCompactMargin.horizontal()),
                        FullMinHeight - PlateCompactMargin.vertical()};
        }
        case Mode::FlyoutPrimary:
            return bounds().inset(Thickness{FlyoutPrimaryPlate});
        case Mode::FullSize:
        case Mode::LabelCollapsed:
        case Mode::LabelOnRight:
            break;
    }
    return bounds().inset(PlateMargin);
}

void AppBarButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme&                             theme   = context.theme;
    const Mode                               m       = mode();
    const std::optional<std::optional<bool>> toggle  = toggle_state();
    const bool                               checked = toggle.has_value() && toggle->value_or(false);
    ButtonState                              state   = visual_state();
    // A button whose flyout is open keeps the pointer-over look.
    if (state == ButtonState::Normal && flyout() != nullptr && flyout()->is_open())
    {
        state = ButtonState::Hover;
    }

    ButtonVisual visual = style_visual(theme, ButtonVariant::Subtle, state);
    if (checked && m != Mode::Overflow)
    {
        // A checked AppBarToggleButton takes the accent button's colors. An era with no accent
        // button shows checked as pushed in, as ToggleButton does.
        const ButtonVisual accent     = style_visual(theme, ButtonVariant::Accent, ButtonState::Normal);
        const ButtonVisual standard   = style_visual(theme, ButtonVariant::Neutral, ButtonState::Normal);
        const bool         has_accent = accent.background != standard.background || accent.text != standard.text;
        visual = has_accent ? style_visual(theme, ButtonVariant::Accent, state)
                            : style_visual(theme,
                                           ButtonVariant::Neutral,
                                           state == ButtonState::Disabled ? state : ButtonState::Pressed);
    }

    const Rect plate = plate_rect();
    draw_control_chrome(renderer, plate, theme.corner_radius, visual, theme.border_width);

    const IconSource& icon_source = icon();
    const std::string text        = this->text();
    const Color       foreground  = visual.text;
    const Color       secondary   = is_enabled() ? theme.surfaces.menu_secondary_text.value_or(foreground) : foreground;

    switch (m)
    {
        case Mode::FullSize:
        case Mode::Compact:
        case Mode::LabelCollapsed:
        {
            // The icon's square, centered over the label: a wide picture takes no more room than a glyph.
            icon_source.draw(renderer,
                             Rect{bounds().center().x - AppBarButtonIconSize * 0.5f,
                                  bounds().y + IconTop,
                                  AppBarButtonIconSize,
                                  AppBarButtonIconSize},
                             foreground);
            if (m == Mode::FullSize && !text.empty())
            {
                const Rect label_rect{bounds().x + LabelMargin.left,
                                      bounds().y + IconTop + AppBarButtonIconSize + IconBottom,
                                      std::max(0.0f, bounds().width - LabelMargin.horizontal()),
                                      label_height_};
                TextStyle  style = label_style(theme);
                style.vertical   = TextAlign::Leading;
                renderer.draw_text(text, label_rect, foreground, style);
            }
            break;
        }
        case Mode::LabelOnRight:
        {
            icon_source.draw(renderer,
                             Rect{bounds().x + RightIconMargin.left,
                                  bounds().y + RightIconMargin.top,
                                  AppBarButtonIconSize,
                                  AppBarButtonIconSize},
                             foreground);
            if (!text.empty())
            {
                TextStyle style  = label_style(theme);
                style.wrapping   = TextWrapping::NoWrap;
                style.horizontal = TextAlign::Leading;
                style.vertical   = TextAlign::Leading;
                const float x    = bounds().x + RightIconMargin.left + AppBarButtonIconSize + RightLabelMargin.left;
                // The chevron's column (measure_override()) is not the label's.
                const float chevron = flyout() != nullptr ? BarChevronLabelOnRightLeft + BarChevronSize + BarChevronRight
                                                          : 0.0f;
                renderer.draw_text(text,
                                   Rect{x,
                                        bounds().y + RightLabelMargin.top,
                                        std::max(0.0f, bounds().right() - chevron - RightLabelMargin.right - x),
                                        label_height_},
                                   foreground,
                                   style);
            }
            break;
        }
        case Mode::FlyoutPrimary:
            icon_source.draw(renderer,
                             Rect{bounds().center().x - AppBarButtonIconSize * 0.5f,
                                  bounds().center().y - AppBarButtonIconSize * 0.5f,
                                  AppBarButtonIconSize,
                                  AppBarButtonIconSize},
                             foreground);
            break;
        case Mode::Overflow:
        {
            const OverflowColumns columns = overflow_columns(look_);
            const float           icon_x  = columns.icon_x;
            const float           label_x = columns.label_x;
            if (checked)
            {
                draw_icon(renderer,
                          IconGlyph{AppBarButtonCheckGlyph, theme.icon_font_family, AppBarButtonCheckGlyphSize},
                          Rect{bounds().x + CheckGlyphInset, bounds().y, AppBarButtonCheckGlyphSize, bounds().height},
                          foreground,
                          AppBarButtonCheckGlyphSize,
                          theme.icon_font_family);
            }
            if (look_.overflow_icons && !icon_source.empty())
            {
                icon_source.draw(renderer,
                                 Rect{bounds().x + icon_x,
                                      bounds().center().y - AppBarButtonIconSize * 0.5f,
                                      AppBarButtonIconSize,
                                      AppBarButtonIconSize},
                                 foreground);
            }
            float right = bounds().right() - OverflowInset;
            if (flyout() != nullptr)
            {
                draw_disclosure(renderer,
                                theme,
                                IconGlyph{AppBarButtonChevronGlyph, theme.icon_font_family, OverflowChevronSize},
                                ArrowDirection::Right,
                                Rect{bounds().right() - OverflowChevronRight - OverflowChevronSize,
                                     bounds().y,
                                     OverflowChevronSize,
                                     bounds().height},
                                secondary,
                                OverflowChevronSize);
                right = bounds().right() - OverflowChevronRight - OverflowChevronSize - OverflowInset;
            }
            if (look_.overflow_shortcut_width > 0.0f)
            {
                const std::string shortcut = shortcut_text();
                const float accel_right = (flyout() != nullptr ? right + OverflowInset : bounds().right()) - ShortcutRight;
                if (!shortcut.empty())
                {
                    TextStyle style  = theme.type.caption;
                    style.horizontal = TextAlign::Trailing;
                    renderer.draw_text(shortcut,
                                       Rect{accel_right - look_.overflow_shortcut_width,
                                            bounds().y,
                                            look_.overflow_shortcut_width,
                                            bounds().height},
                                       secondary,
                                       style);
                }
                right = accel_right - look_.overflow_shortcut_width - AppBarButtonShortcutGap;
            }
            TextStyle style  = theme.text_style;
            style.horizontal = TextAlign::Leading;
            renderer.draw_text(text,
                               Rect{bounds().x + label_x,
                                    bounds().y + columns.padding.top,
                                    std::max(0.0f, right - (bounds().x + label_x)),
                                    std::max(0.0f, bounds().height - columns.padding.vertical())},
                               foreground,
                               style);
            break;
        }
    }

    // The chevron: a button with a flyout says so, pointing down at the menu it opens. Up at
    // its right under a label below the icon; level with the label beside it.
    if (flyout() != nullptr && m != Mode::Overflow && m != Mode::FlyoutPrimary)
    {
        const float top = m == Mode::LabelOnRight && !text.empty()
                              ? bounds().y + RightLabelMargin.top + (label_height_ - BarChevronSize) * 0.5f
                          : m == Mode::LabelOnRight ? bounds().center().y - BarChevronSize * 0.5f
                                                    : bounds().y + BarChevronTop;
        draw_disclosure(renderer,
                        theme,
                        IconGlyph{BarChevronGlyph, theme.icon_font_family, BarChevronSize},
                        ArrowDirection::Down,
                        Rect{bounds().right() - BarChevronRight - BarChevronSize, top, BarChevronSize, BarChevronSize},
                        foreground,
                        BarChevronSize);
    }
}

void AppBarButtonControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    // The standard button's hover wash, over the plate rather than the whole 68x64 button.
    (void)compositor;
    (void)theme;
}

std::string AppBarButtonControl::tool_tip() const
{
    std::string explicit_tip = Element::tool_tip();
    if (!explicit_tip.empty())
    {
        return explicit_tip;
    }
    // A command's description is the tooltip.
    if (command() != nullptr && !command()->current().description.empty())
    {
        return command()->current().description;
    }
    // "Text (Ctrl+S)" outside the overflow.
    const std::string shortcut = shortcut_text();
    if (!look_.overflow && !shortcut.empty())
    {
        return detail::fill(strings().common.with_shortcut, {text(), shortcut});
    }
    return {};
}

bool AppBarButtonControl::handle_global_shortcut(const KeyEvent& e)
{
    if (!is_enabled() || !shortcut().matches(e))
    {
        return false;
    }
    activate(); // last: a handler may tear the page down
    return true;
}

void AppBarButtonControl::on_activated()
{
    // A click on a command closes its bar, unless it opens a flyout.
    if (command_host_ != nullptr && flyout() == nullptr)
    {
        command_host_->command_invoked(*this);
    }
}

// ---- AppBarToggleButton ----

AppBarToggleButtonControl::AppBarToggleButtonControl(std::string text, IconSource icon, std::optional<bool> checked) :
    AppBarButtonControl(std::move(text), std::move(icon)),
    checked_(checked)
{
}

AppBarToggleButtonControl::AppBarToggleButtonControl() = default;

void AppBarToggleButtonControl::set_checked(std::optional<bool> checked)
{
    if (checked_ == checked)
    {
        return;
    }
    checked_ = checked;
    mark_dirty(DirtyFlags::Visual);
    if (on_checked_changed_)
    {
        on_checked_changed_(checked_);
    }
}

void AppBarToggleButtonControl::on_activated()
{
    // Unchecked, checked, then indeterminate when three-state; a two-state button checks from
    // indeterminate, and one of a set of choices (ui::selects()) stays checked.
    if (checked_ == true)
    {
        if (!choice_)
        {
            set_checked(three_state() ? std::nullopt : std::optional<bool>{false});
        }
    }
    else
    {
        set_checked(!(three_state() && !checked_.has_value()));
    }
    AppBarButtonControl::on_activated();
}

bool AppBarToggleButtonControl::toggle_next()
{
    if (!is_enabled())
    {
        return false;
    }
    on_activated();
    return true;
}

// ---- AppBarSeparator ----

void AppBarSeparatorControl::set_app_bar_look(const AppBarLook& look)
{
    if (look_ == look)
    {
        return;
    }
    look_ = look;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

Size AppBarSeparatorControl::measure_override(Size /*available*/, const MeasureContext&) const
{
    // AppBarSeparator: 1 wide in a 2,8,2,8 margin in the bar (48 tall while
    // compact), 1 high in a 0,4,0,4 margin in the overflow.
    if (look_.overflow)
    {
        return Size{0.0f, 9.0f};
    }
    return Size{5.0f,
                look_.compact || look_.text_position != CommandBarDefaultTextPosition::Bottom
                    ? AppBarButtonCompactHeight
                    : FullMinHeight};
}

void AppBarSeparatorControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const Color  color = theme.divider_visual == ThemeDividerVisual::FlatStroke
                             ? theme.colors().divider
                             : theme.surfaces.separator_dark;
    if (look_.overflow)
    {
        renderer.fill_rounded_rect(Rect{bounds().x, bounds().y + 4.0f, bounds().width, 1.0f}, 0.0f, color);
        return;
    }
    const float height = look_.compact ? AppBarButtonCompactHeight : bounds().height;
    renderer.fill_rounded_rect(Rect{bounds().x + 2.0f, bounds().y + 8.0f, 1.0f, std::max(0.0f, height - 16.0f)}, 0.5f, color);
}

namespace
{
// What ui::AppBarButton and ui::AppBarToggleButton share.
template <typename S>
void apply_app_bar_button(S& spec, ui::AppBarButtonControl& node)
{
    apply(spec.text, node, [raw = &node](const std::string& value) { raw->set_text(value); });
    apply(spec.icon, node, [raw = &node](const ui::IconSource& value) { raw->set_icon(value); });
    apply(spec.text_position,
          node,
          [raw = &node](const ui::CommandBarTextPosition& value) { raw->set_text_position(value); });
    node.set_compact(spec.compact);
    apply(spec.shortcut, node, [raw = &node](const ui::KeyCombo& value) { raw->set_shortcut(value); });
    apply(spec.dynamic_overflow_order, node, [raw = &node](const int& value) { raw->set_dynamic_overflow_order(value); });
    // `.enabled` and the command's `enabled` both have to allow it (ui::Button's rule).
    apply(spec.enabled, node, [raw = &node](const bool& value) { raw->set_enabled(value); });
    if (spec.command != nullptr)
    {
        node.set_command(spec.command, std::move(spec.command_parameter));
    }
}
} // namespace

std::unique_ptr<ui::Element> AppBarButton::build() &&
{
    auto node = std::make_unique<ui::AppBarButtonControl>();
    apply_app_bar_button(*this, *node);
    if (on_click)
    {
        node->on_click(std::move(on_click));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

std::unique_ptr<ui::Element> AppBarToggleButton::build() &&
{
    auto node = std::make_unique<ui::AppBarToggleButtonControl>();
    apply_app_bar_button(*this, *node);
    assert((!three_state || !checked.writes_bool()) &&
           "three_state needs `checked` bound to a Property<std::optional<bool>>: a click can reach "
           "indeterminate, which a Property<bool> cannot hold");
    node->set_three_state(three_state);
    node->choice_ = checked.selection_group() != nullptr;
    apply(checked, *node, [raw = node.get()](const std::optional<bool>& value) { raw->set_checked(value); });
    if (auto changed = ui::write_back(checked, std::move(on_checked_changed)))
    {
        node->on_checked_changed(std::move(changed));
    }
    if (on_click)
    {
        node->on_click(std::move(on_click));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

std::unique_ptr<ui::Element> AppBarSeparator::build() &&
{
    auto node = std::make_unique<ui::AppBarSeparatorControl>();
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

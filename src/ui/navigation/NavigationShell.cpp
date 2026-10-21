// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/NavigationShell.hpp"

#include "../../render/Compositor.hpp"
#include "../../render/ControlGlyphs.hpp"
#include "../LocalText.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/media/AnimatedVisuals.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float NavigationShellHoverOverlayAlpha = 0.10f; // non-Fluent fallback wash, see render_left_rows()

// ---- Item rows ----

// A 4,2 margin around the 36px pill of a left-pane row.
constexpr float ItemMarginH = 4.0f;
constexpr float ItemMarginV = 2.0f;

// Without an icon, an 8px icon column plus the label's 4,-1,8,-1 margin. The trailing inset
// adds the row's 14px right margin.
constexpr float NoIconColumn       = 8.0f;
constexpr float LabelLead          = 4.0f;
constexpr float ItemTextTrailInset = 8.0f + 14.0f;

// The icon column: the compact pane length less 8, with the 16px icon scaled to fit and
// centered in it.
constexpr float IconColumnWidth         = 48.0f - 8.0f;
constexpr float NavigationShellIconSize = 16.0f;

// The badge sits against the row's 14px right margin. Closed compact moves a top-level item's
// badge to the pill's top-right corner, 2 in from the top and the right.
constexpr float BadgeTrailInset   = 14.0f;
constexpr float CompactBadgeInset = 2.0f;

// The indent per nesting level, a left margin on the row's content: the text steps in,
// the pill and the indicator stay flush with the pane.
constexpr float IndentPerDepth = 31.0f;

// The chevron: a 40px box flush with the pill (its margin cancels the row's 14px right margin),
// so it takes 26px of the label. Its glyph is 8.
constexpr float ChevronBoxWidth                   = 40.0f;
constexpr float NavigationShellChevronGlyphSize   = 8.0f;
constexpr float NavigationShellChevronColumnWidth = ChevronBoxWidth - 14.0f;

// A section header: 40 tall, inset 16,0 in the left pane and 12,0 in the top bar.
constexpr float HeaderRowHeight = 40.0f;
constexpr float HeaderInsetLeft = 16.0f;
constexpr float HeaderInsetTop  = 12.0f;

// A separator entry: 1px plus a 0,3,0,4 margin, and 2 more for the separator between the menu
// items and the footer. In the top bar the margin is 3,0,4,0.
constexpr float SeparatorRowHeight  = 1.0f + 3.0f + 4.0f;
constexpr float PaneSeparatorHeight = SeparatorRowHeight + 2.0f;
constexpr float TopSeparatorWidth   = 1.0f + 3.0f + 4.0f;

// ---- The pane ----

// The pane's 1px border plus its content's -1,3 margin. The horizontal -1 cancels
// the border.
constexpr float PaneTopInset    = 1.0f + 3.0f;
constexpr float PaneBottomInset = 1.0f + 3.0f;
// The back button's row and the width the toggle button moves over for it, then the toggle
// button's row.
constexpr float BackRowHeight   = 40.0f;
constexpr float BackColumnWidth = 40.0f;
constexpr float ToggleRowHeight = 40.0f;
// The toggle and back buttons' size.
constexpr float NavigationShellButtonWidth  = 40.0f;
constexpr float NavigationShellButtonHeight = 36.0f;
// The buttons' column has a 0,4 margin, and every button a 4,2 margin.
constexpr float ButtonHolderTop = 4.0f;
// The search box's row: 40 tall with 8 below it, the box inset 16,0.
constexpr float AsbRowHeight = 40.0f;
constexpr float AsbRowBottom = 8.0f;
constexpr float AsbInset     = 16.0f;
constexpr float FooterBottom = 4.0f; // the footer's bottom margin

// ---- The top bar ----

// The top bar's height and inset, the custom content, overflow button and AutoSuggestBox
// minimums, and Settings' 40px square.
constexpr float TopBarHeight       = 48.0f;
constexpr float TopBarInset        = 4.0f;
constexpr float TopCustomMinWidth  = 48.0f;
constexpr float OverflowButtonSize = 40.0f;
constexpr float TopAsbMinWidth     = 216.0f;
constexpr float TopAsbMargin       = 4.0f;
constexpr float TopSettingsWidth   = 40.0f;
// A top-bar item's label: an 8,-1,12,-1 margin after a 12px icon lead, or 12,0 without an icon.
// An icon-only item is 36 square with a 2 margin.
constexpr float TopIconLead     = 12.0f;
constexpr float TopIconGap      = 8.0f;
constexpr float TopLabelTrail   = 12.0f;
constexpr float TopIconOnlySize = 36.0f;
constexpr float TopIconOnlySlot = TopIconOnlySize + 4.0f;
// The top bar's indicator: a 16x3 bar 4 above the pill's bottom, centered.
constexpr float TopIndicatorWidth  = 16.0f;
constexpr float TopIndicatorHeight = 3.0f;
constexpr float TopIndicatorBottom = 4.0f;

// ---- The content ----

// The header's margin, 56,44 (-24,44 in Minimal) after the content's left padding. Its minimum
// height is the toggle button's.
constexpr float HeaderLeft                     = 56.0f;
constexpr float HeaderLeftMinimal              = -24.0f;
constexpr float HeaderTop                      = 44.0f;
constexpr float NavigationShellHeaderMinHeight = 36.0f;
// The content's top-left corner radius in Expanded and Compact.
constexpr float ContentCornerRadius = 8.0f;

// ---- Flyouts ----

// The children flyout's padding, a flyout's minimum and maximum widths, and a row in the
// overflow flyout.
constexpr float NavigationShellFlyoutPadding  = 2.0f;
constexpr float FlyoutCompactLift             = 4.0f;
constexpr float NavigationShellFlyoutMinWidth = 96.0f;
constexpr float NavigationShellFlyoutMaxWidth = 456.0f;
constexpr float OverflowRowHeight             = 36.0f;
constexpr float OverflowIconLead              = 16.0f;
constexpr float OverflowLabelGap              = 12.0f;
constexpr float OverflowLabelTrail            = 20.0f + 14.0f;
constexpr float OverflowIndicatorW            = 2.0f;
constexpr float OverflowIndicatorH            = 24.0f;
constexpr float OverflowIndicatorPad          = 4.0f;

// The indicator's move: the leading edge accelerates away over the first 200ms, then the
// trailing edge decelerates into place over the remaining 400ms.
constexpr double      IndicatorTotalMs = 600.0;
constexpr double      IndicatorLeadMs  = 200.0;
constexpr CubicBezier IndicatorLead{0.9f, 0.1f, 1.0f, 0.2f};  // the first 200ms
constexpr CubicBezier IndicatorTrail{0.1f, 0.9f, 0.2f, 1.0f}; // the remaining 400ms

// The pane's open and close transitions, the same four SidePane.cpp uses.
constexpr double      NavigationShellOverlayOpenMs  = 350.0;
constexpr double      NavigationShellOverlayCloseMs = 120.0;
constexpr double      NavigationShellInlineOpenMs   = 200.0;
constexpr double      NavigationShellInlineCloseMs  = 100.0;
constexpr CubicBezier NavigationShellOverlayCurve{0.1f, 0.9f, 0.2f, 1.0f};
constexpr CubicBezier NavigationShellInlineCurve{0.0f, 0.35f, 0.15f, 1.0f};

// The toggle, back and search buttons' glyphs.
constexpr IconGlyph HamburgerGlyph{0xE700, "Segoe Fluent Icons", 16.0f};
constexpr IconGlyph BackGlyph{0xE72B, "Segoe Fluent Icons", 16.0f};
constexpr IconGlyph SearchGlyph{0xE721, "Segoe Fluent Icons", 14.0f};

// The item's background pill: a row's slot inset by the margins above. Hit-testing still uses
// the full slot so the gap between rows stays clickable.
Rect inset_item_row(Rect row)
{
    return Rect{row.left() + ItemMarginH,
                row.top() + ItemMarginV,
                std::max(0.0f, row.width - 2.0f * ItemMarginH),
                std::max(0.0f, row.height - 2.0f * ItemMarginV)};
}

ButtonState row_state(bool enabled, bool hovered, bool pressed)
{
    if (!enabled)
    {
        return ButtonState::Disabled;
    }
    if (pressed && hovered)
    {
        return ButtonState::Pressed;
    }
    return hovered ? ButtonState::Hover : ButtonState::Normal;
}

TextStyle item_label_style(const Theme& theme)
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    return style;
}

// A section header's text: the body style at 14, SemiBold.
TextStyle header_item_style(const Theme& theme)
{
    TextStyle style  = theme.type.for_role(TextRole::BodyStrong);
    style.weight     = FontWeight::SemiBold;
    style.horizontal = TextAlign::Leading;
    return style;
}

// A divider in the era's own shape (Toolbar.cpp's separator does the same).
void draw_divider(Renderer& renderer, const Theme& theme, const Rect& line, bool vertical)
{
    if (theme.divider_visual == ThemeDividerVisual::FlatStroke)
    {
        renderer.fill_rounded_rect(line, 0.0f, theme.colors().divider);
        return;
    }
    const Rect second = vertical ? Rect{line.x + line.width, line.y, line.width, line.height}
                                 : Rect{line.x, line.y + line.height, line.width, line.height};
    renderer.fill_rounded_rect(line, 0.0f, theme.surfaces.separator_dark);
    renderer.fill_rounded_rect(second, 0.0f, theme.surfaces.separator_light);
}

// An element that takes no room and draws nothing: the rows' ToolTip anchor, moved under the
// hovered row.
class NavigationShellEmptyAnchor final : public Element
{
public:

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return Size{};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};

std::unique_ptr<ToolTipControl> make_tip(std::unique_ptr<Element> anchor, std::string text)
{
    auto tip = std::make_unique<ToolTipControl>();
    tip->set_anchor(std::move(anchor));
    tip->set_content(std::make_unique<TextControl>(std::move(text)));
    return tip;
}

void set_tip_text(ToolTipControl& tip, std::string text)
{
    if (auto* block = dynamic_cast<TextControl*>(tip.content()); block != nullptr && block->text() != text)
    {
        block->set_text(std::move(text));
    }
}

// Named by the shell once built, from strings().navigation_shell.
std::unique_ptr<ButtonControl> make_glyph_button(IconGlyph glyph)
{
    auto button = std::make_unique<ButtonControl>();
    button->set_variant(ButtonVariant::Subtle);
    button->set_padding(Thickness{0.0f});
    button->set_content(std::make_unique<IconControl>(glyph));
    return button;
}

} // namespace

// ---- The toggle button's content: the hamburger in its 40px column, then the pane title ----

// The pane title has a 4,0,0,0 padding and a 0,-2,0,0 margin.
class NavPaneButtonContent final : public Element
{
public:
    void set_title(std::string title)
    {
        if (title != title_)
        {
            title_ = std::move(title);
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return Size{NavigationShellButtonWidth, NavigationShellButtonHeight};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        renderer.push_clip(bounds());
        draw_icon(renderer,
                  HamburgerGlyph,
                  Rect{bounds().x, bounds().y, NavigationShellButtonWidth, bounds().height},
                  theme.colors().text,
                  HamburgerGlyph.size,
                  theme.icon_font_family);
        if (!title_.empty() && bounds().width > NavigationShellButtonWidth + 4.0f)
        {
            const Rect text{bounds().x + NavigationShellButtonWidth + 4.0f,
                            bounds().y - 2.0f,
                            bounds().width - NavigationShellButtonWidth - 4.0f,
                            bounds().height};
            renderer.draw_text(title_, text, theme.colors().text, header_item_style(theme));
        }
        renderer.pop_clip();
    }


private:
    std::string title_;
};

// ---- The children/overflow flyout's rows ----

// Only a surface: every row is the owner's state, drawn and hit-tested through the owner.
class NavFlyoutList final : public Element
{
public:
    explicit NavFlyoutList(NavigationShellControl& owner) : owner_(owner)
    {
    }

    std::size_t hovered = detail::NoIndex; // row
    std::size_t pressed = detail::NoIndex;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    bool               pointer_override(const PointerEvent& e) override;
    // row

private:
    [[nodiscard]] float       row_height(const NavigationShellControl::Row& row) const;
    [[nodiscard]] std::size_t row_at(Point p) const; // an item row under `p`, or detail::NoIndex
    [[nodiscard]] float natural_row_width(const NavigationShellControl::Row& row, const MeasureContext& context) const;

    NavigationShellControl& owner_;

protected:
    /// Rings the keyboard's row itself: it shows the ring while the shell keeps focus, so the
    /// window's ring pass never sees it.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }
};

// ---- construction ----

NavigationShellControl::NavigationShellControl()
{
    static const AnimatedSettingsVisualSource SettingsVisual;
    settings_icon_ = std::make_unique<AnimatedIconControl>();
    settings_icon_->set_source(&SettingsVisual);
    settings_icon_->set_fallback(symbol_glyph(Symbol::Setting, NavigationShellIconSize));
    settings_icon_->set_state("Normal"); // a row's icon starts in its resting state
    set_parent_of(*settings_icon_, this);

    back_tip_    = make_tip(make_glyph_button(BackGlyph), std::string{});
    back_button_ = static_cast<ButtonControl*>(back_tip_->anchor());
    set_parent_of(*back_tip_, this);
    back_button_->on_click(
        [this]
        {
            if (on_back_requested_)
            {
                on_back_requested_();
            }
        });

    // The close button shows the back button's arrow.
    close_tip_    = make_tip(make_glyph_button(BackGlyph), std::string{});
    close_button_ = static_cast<ButtonControl*>(close_tip_->anchor());
    set_parent_of(*close_tip_, this);
    close_button_->on_click([this] { request_pane_open(false, false); });

    auto toggle = std::make_unique<ButtonControl>();
    toggle->set_variant(ButtonVariant::Subtle);
    toggle->set_padding(Thickness{0.0f});
    toggle_content_ = toggle->set_content(std::make_unique<NavPaneButtonContent>());
    toggle_tip_     = make_tip(std::move(toggle), std::string{});
    toggle_button_  = static_cast<ButtonControl*>(toggle_tip_->anchor());
    set_parent_of(*toggle_tip_, this);
    toggle_button_->on_click(
        [this]
        {
            // A close by the button is a forced close,
            // which growing back into Expanded respects.
            const bool open   = !pane_open_;
            was_force_closed_ = !open;
            request_pane_open(open, false);
        });

    search_button_owned_ = make_glyph_button(SearchGlyph);
    search_button_       = search_button_owned_.get();
    set_parent_of(*search_button_, this);
    search_button_->on_click(
        [this]
        {
            // The search button opens the pane. The AutoSuggestBox gets focus as the focus trap's
            // first stop.
            was_force_closed_ = false;
            request_pane_open(true, false);
        });

    row_tip_ = make_tip(std::make_unique<NavigationShellEmptyAnchor>(), std::string{});
    set_parent_of(*row_tip_, this);

    flyout_list_ = std::make_unique<NavFlyoutList>(*this);
    set_parent_of(*flyout_list_, this);

    // The buttons' names and tooltips follow set_strings(); sync_buttons() names the pane button.
    detail::follow_strings(*this,
                           [this]
                           {
                               const Strings::NavigationShellText& t = strings().navigation_shell;
                               back_button_->set_accessibility(Accessibility{.name = t.back});
                               set_tip_text(*back_tip_, t.back);
                               close_button_->set_accessibility(Accessibility{.name = t.close});
                               set_tip_text(*close_tip_, t.close_navigation);
                               search_button_->set_accessibility(Accessibility{.name = t.search});
                               sync_buttons();
                           });
}

NavigationShellControl::~NavigationShellControl() = default;

// ---- the item tree, and the derived views over it ----

void NavigationShellControl::walk(std::vector<NavEntry>& entries, std::size_t depth, std::size_t parent)
{
    for (NavEntry& e : entries)
    {
        const std::size_t index = all_.size();
        all_.push_back(&e);
        parent_of_.push_back(parent);
        depth_.push_back(depth);
        if (!e.children.empty())
        {
            has_groups_ = true;
            // Collapsed groups too: a hidden item still owns its pre-order index.
            walk(e.children, depth + 1, index);
        }
    }
}

void NavigationShellControl::rebuild_rows()
{
    all_.clear();
    parent_of_.clear();
    depth_.clear();
    has_groups_ = false;
    walk(entries_, 0, detail::NoIndex);
    footer_begin_ = all_.size();
    walk(footer_entries_, 0, detail::NoIndex);
    rebuild_visible_rows();
}

void NavigationShellControl::rebuild_visible_rows()
{
    menu_rows_.clear();
    footer_rows_.clear();
    // Closed compact and the top bar show top-level items only; a group's children open in a
    // flyout there.
    const bool top_level_only = top_mode() || closed_compact();
    for (std::size_t i = 0; i < all_.size(); ++i)
    {
        if (top_level_only && depth_[i] > 0)
        {
            continue;
        }
        bool visible = true;
        for (std::size_t p = parent_of_[i]; p != detail::NoIndex; p = parent_of_[p])
        {
            if (!all_[p]->expanded)
            {
                visible = false;
                break;
            }
        }
        if (visible)
        {
            (i < footer_begin_ ? menu_rows_ : footer_rows_).push_back(Row{i, depth_[i]});
        }
    }
    if (settings_visible_)
    {
        footer_rows_.push_back(Row{Settings, 0});
    }
}

NavEntry* NavigationShellControl::entry(std::size_t index) const
{
    return index < all_.size() ? all_[index] : nullptr;
}

bool NavigationShellControl::selectable(std::size_t index) const
{
    if (index == Settings)
    {
        return settings_visible_;
    }
    const NavEntry* e = entry(index);
    return e != nullptr && e->kind == NavEntryKind::Item && e->selects_on_invoke;
}

bool NavigationShellControl::cursor_stop(std::size_t index) const
{
    if (index == Settings)
    {
        return settings_visible_;
    }
    const NavEntry* e = entry(index);
    return e != nullptr && e->kind == NavEntryKind::Item;
}

bool NavigationShellControl::is_footer(std::size_t index) const
{
    return index == Settings || (index < all_.size() && index >= footer_begin_);
}

std::size_t NavigationShellControl::parent_of(std::size_t index) const
{
    return index < parent_of_.size() ? parent_of_[index] : detail::NoIndex;
}

std::size_t NavigationShellControl::depth_of(std::size_t index) const
{
    return index < depth_.size() ? depth_[index] : 0;
}

const std::string& NavigationShellControl::text_at(std::size_t index) const
{
    static const std::string empty;
    if (index == Settings)
    {
        return strings().navigation_shell.settings;
    }
    return index < all_.size() ? all_[index]->text : empty;
}

const std::string& NavigationShellControl::key_at(std::size_t index) const
{
    static const std::string empty;
    static const std::string settings{SettingsKey};
    if (index == Settings)
    {
        return settings;
    }
    if (index >= all_.size())
    {
        return empty;
    }
    return all_[index]->key.empty() ? all_[index]->text : all_[index]->key;
}

std::optional<std::size_t> NavigationShellControl::index_of(std::string_view key) const
{
    if (key == SettingsKey)
    {
        return Settings;
    }
    for (std::size_t i = 0; i < all_.size(); ++i)
    {
        if (key_at(i) == key)
        {
            return i;
        }
    }
    return std::nullopt;
}

Element* NavigationShellControl::active_content() const
{
    if (selected_index_ < all_.size() && all_[selected_index_]->content)
    {
        return all_[selected_index_]->content.get();
    }
    return content_.get();
}

InfoBadgeControl* NavigationShellControl::info_badge_at(std::size_t index) const
{
    return index < all_.size() ? all_[index]->info_badge.get() : nullptr;
}

InfoBadgeControl* NavigationShellControl::set_info_badge(std::size_t index, std::unique_ptr<InfoBadgeControl> badge)
{
    if (index >= all_.size())
    {
        return nullptr;
    }
    InfoBadgeControl* observer = badge.get();
    if (observer != nullptr)
    {
        set_parent_of(*observer, this);
    }
    all_[index]->info_badge = std::move(badge);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return observer;
}

void NavigationShellControl::set_text(std::size_t index, std::string text)
{
    if (index < all_.size() && all_[index]->text != text)
    {
        all_[index]->text = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void NavigationShellControl::set_selects_on_invoke(std::size_t index, bool selects)
{
    if (index < all_.size())
    {
        all_[index]->selects_on_invoke = selects;
    }
}

namespace
{
void adopt_entries(Element* owner, std::vector<NavEntry>& entries)
{
    for (NavEntry& e : entries)
    {
        if (e.content)
        {
            detail::TreeAccess::set_parent(*e.content, owner);
        }
        if (e.info_badge)
        {
            detail::TreeAccess::set_parent(*e.info_badge, owner);
        }
        if (e.animated_icon)
        {
            detail::TreeAccess::set_parent(*e.animated_icon, owner);
            if (e.animated_icon->state().empty())
            {
                e.animated_icon->set_state("Normal"); // a row's icon starts in its resting state
            }
        }
        adopt_entries(owner, e.children);
    }
}
} // namespace

void NavigationShellControl::set_items(std::vector<NavEntry> items)
{
    const bool keep_settings = selected_index_ == Settings;
    close_flyout();
    entries_ = std::move(items);
    adopt_entries(this, entries_);
    rebuild_rows();

    // Every piece of state below is phrased in terms of the tree that just went away.
    selected_index_     = keep_settings ? Settings : detail::NoIndex;
    cursor_index_       = selected_index_;
    hovered_item_       = detail::NoIndex;
    pressed_item_       = detail::NoIndex;
    indicator_from_     = std::nullopt;
    indicator_ms_       = 0.0;
    pane_scroll_offset_ = 0.0f;
    if (!keep_settings)
    {
        for (std::size_t i = 0; i < all_.size(); ++i)
        {
            if (selectable(i))
            {
                selected_index_ = i;
                cursor_index_   = i;
                break;
            }
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::set_footer_items(std::vector<NavEntry> items)
{
    close_flyout();
    const std::size_t old_begin = footer_begin_;
    const bool was_footer = selected_index_ != Settings && selected_index_ != detail::NoIndex && selected_index_ >= old_begin;
    footer_entries_ = std::move(items);
    adopt_entries(this, footer_entries_);
    rebuild_rows();
    if (was_footer)
    {
        selected_index_ = detail::NoIndex;
    }
    if (cursor_index_ != Settings && cursor_index_ != detail::NoIndex && cursor_index_ >= footer_begin_)
    {
        cursor_index_ = selected_index_;
    }
    hovered_item_         = detail::NoIndex;
    pressed_item_         = detail::NoIndex;
    indicator_from_       = std::nullopt;
    footer_scroll_offset_ = 0.0f;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- selection ----

void NavigationShellControl::set_selected_index(std::optional<std::size_t> requested)
{
    std::size_t index = detail::index_or_none(requested);
    if (index == detail::NoIndex)
    {
        if (selected_index_ != detail::NoIndex)
        {
            indicator_from_ = std::nullopt;
            selected_index_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
            if (on_selection_changed_)
            {
                on_selection_changed_(std::string{});
            }
        }
        return;
    }
    if (index != Settings)
    {
        if (all_.empty())
        {
            return;
        }
        index = std::min(index, all_.size() - 1);
    }
    select(index);
    if (selected_index_ == index)
    {
        reveal_pending_ = index;
        mark_dirty(DirtyFlags::Layout);
    }
}

void NavigationShellControl::set_selected_key(std::string_view key)
{
    if (key.empty())
    {
        set_selected_index(std::nullopt);
        return;
    }
    if (const std::optional<std::size_t> index = index_of(key))
    {
        set_selected_index(index);
    }
}

void NavigationShellControl::select(std::size_t index)
{
    if (index == selected_index_ || !selectable(index))
    {
        return;
    }
    // Gated on `ticking_` like Expander's slide: with no frame clock the indicator must land at once
    // instead of freezing mid-stretch.
    if (ticking_ && selected_index_ != detail::NoIndex)
    {
        indicator_from_ = std::nullopt;
        if (top_mode())
        {
            for (const TopSlot& slot : top_slots_)
            {
                std::size_t shown = selected_index_;
                while (shown != detail::NoIndex && shown != Settings && parent_of(shown) != detail::NoIndex)
                {
                    shown = parent_of(shown);
                }
                if (slot.item == shown)
                {
                    indicator_from_ = slot.rect;
                }
            }
        }
        else
        {
            indicator_from_ = item_bounds(selected_index_);
            if (!indicator_from_)
            {
                for (std::size_t p = parent_of(selected_index_); p != detail::NoIndex && !indicator_from_; p = parent_of(p))
                {
                    indicator_from_ = item_bounds(p);
                }
            }
        }
        indicator_ms_ = 0.0;
    }
    selected_index_ = index;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(key_at(selected_index_));
    }
}

bool NavigationShellControl::expanded(std::size_t index) const
{
    return index < all_.size() && all_[index]->expanded;
}

void NavigationShellControl::set_expanded(std::size_t index, bool expand)
{
    if (index >= all_.size() || all_[index]->children.empty() || all_[index]->expanded == expand)
    {
        return;
    }
    all_[index]->expanded = expand;
    rebuild_visible_rows();
    if (flyout_kind_ != FlyoutKind::None)
    {
        rebuild_flyout_rows();
    }

    // Every row position in flight refers to the layout that just changed underneath it.
    indicator_from_ = std::nullopt;
    indicator_ms_   = 0.0;
    hovered_item_   = detail::NoIndex;
    pressed_item_   = detail::NoIndex;
    // A cursor swallowed by the group it was inside comes back out onto the group itself.
    if (cursor_index_ != detail::NoIndex && cursor_index_ != Settings)
    {
        for (std::size_t p = parent_of(cursor_index_); p != detail::NoIndex; p = parent_of(p))
        {
            if (!all_[p]->expanded)
            {
                cursor_index_ = p;
            }
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_expanded_changed_)
    {
        on_expanded_changed_(NavigationShellExpandedChangedArgs{.index = index, .expanded = expand});
    }
}

void NavigationShellControl::invoke(std::size_t index, bool chevron_only, bool from_flyout)
{
    const NavEntry* e = entry(index);
    if (index != Settings && (e == nullptr || e->kind != NavEntryKind::Item))
    {
        return;
    }
    const bool group = e != nullptr && !e->children.empty();

    // The chevron is its own hit target and does nothing but toggle.
    if (chevron_only && group && !from_flyout)
    {
        if (!top_mode() && !closed_compact())
        {
            set_expanded(index, !e->expanded);
        }
        else
        {
            open_children_flyout(index);
        }
        return;
    }

    cursor_index_ = from_flyout ? cursor_index_ : index;
    if (on_item_invoked_)
    {
        on_item_invoked_(key_at(index));
    }
    // Select first, then toggle the group or open its flyout.
    select(index);
    if (group)
    {
        const bool flyout_parent = !from_flyout && (top_mode() || (closed_compact() && depth_of(index) == 0));
        if (flyout_parent)
        {
            if (flyout_kind_ == FlyoutKind::Children && flyout_group_ == index)
            {
                close_flyout();
            }
            else
            {
                open_children_flyout(index);
            }
        }
        else
        {
            set_expanded(index, !e->expanded);
        }
    }
    else
    {
        // A leaf closes the flyout
        // it was in, and an overlaying pane.
        close_flyout();
        if (selectable(index) && pane_open_ && display_mode_ != NavigationShellDisplayMode::Expanded && !top_mode())
        {
            request_pane_open(false, false);
        }
    }
    mark_dirty(DirtyFlags::Visual);
}

// ---- the flyout ----

void NavigationShellControl::rebuild_flyout_rows()
{
    flyout_rows_.clear();
    const auto add_subtree = [this](std::size_t root, std::size_t base_depth)
    {
        // The root's descendants in pre-order, shown while every ancestor below `root` is expanded.
        const std::size_t root_depth = depth_[root];
        for (std::size_t i = root + 1; i < all_.size() && depth_[i] > root_depth; ++i)
        {
            bool visible = true;
            for (std::size_t p = parent_of_[i]; p != root; p = parent_of_[p])
            {
                if (!all_[p]->expanded)
                {
                    visible = false;
                    break;
                }
            }
            if (visible)
            {
                flyout_rows_.push_back(Row{i, base_depth + depth_[i] - root_depth - 1});
            }
        }
    };
    if (flyout_kind_ == FlyoutKind::Children && flyout_group_ < all_.size())
    {
        add_subtree(flyout_group_, 0);
    }
    else if (flyout_kind_ == FlyoutKind::Overflow)
    {
        for (std::size_t item : overflow_items_)
        {
            flyout_rows_.push_back(Row{item, 0});
            if (all_[item]->expanded)
            {
                // Overflow items show their children inline, one level in.
                const std::size_t before = flyout_rows_.size();
                add_subtree(item, 1);
                (void)before;
            }
        }
    }
    flyout_list_->hovered = detail::NoIndex;
    flyout_list_->pressed = detail::NoIndex;
    set_parent_of(*flyout_list_, this);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    sync_popup();
}

void NavigationShellControl::open_children_flyout(std::size_t group)
{
    flyout_kind_  = FlyoutKind::Children;
    flyout_group_ = group;
    rebuild_flyout_rows();
    flyout_cursor_ = flyout_rows_.empty() ? detail::NoIndex : flyout_rows_.front().item;
    mark_dirty(DirtyFlags::Visual);
}

void NavigationShellControl::open_overflow_flyout()
{
    flyout_kind_  = FlyoutKind::Overflow;
    flyout_group_ = detail::NoIndex;
    rebuild_flyout_rows();
    flyout_cursor_ = flyout_rows_.empty() ? detail::NoIndex : flyout_rows_.front().item;
    mark_dirty(DirtyFlags::Visual);
}

void NavigationShellControl::close_flyout()
{
    if (flyout_kind_ == FlyoutKind::None)
    {
        return;
    }
    flyout_kind_        = FlyoutKind::None;
    flyout_group_       = detail::NoIndex;
    flyout_cursor_      = detail::NoIndex;
    flyout_by_keyboard_ = false;
    flyout_rows_.clear();
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void NavigationShellControl::sync_popup()
{
    if (flyout_kind_ == FlyoutKind::None || flyout_rows_.empty())
    {
        close_popup();
        return;
    }
    // Below in Top mode, to the right of the compact strip otherwise; place_flyout() decides.
    open_popup({
        .content   = flyout_list_.get(),
        .placement = top_mode() ? PopupPlacement::Below : PopupPlacement::Right,
        .place     = [this](Element& content, Size client, const MeasureContext& context)
        { return place_flyout(content, client, context); },
        .on_dismiss = [this](PopupCloseReason) { close_flyout(); },
    });
}

Rect NavigationShellControl::flyout_anchor() const
{
    if (flyout_kind_ == FlyoutKind::Overflow)
    {
        return overflow_button_bounds().value_or(Rect{});
    }
    if (top_mode())
    {
        for (const TopSlot& slot : top_slots_)
        {
            if (slot.item == flyout_group_)
            {
                return slot.rect;
            }
        }
        return Rect{};
    }
    if (auto pill = item_bounds(flyout_group_))
    {
        // The whole row, not its pill: the flyout starts at the row's slot edge.
        return Rect{pill->x - ItemMarginH,
                    pill->y - ItemMarginV,
                    pill->width + 2.0f * ItemMarginH,
                    pill->height + 2.0f * ItemMarginV};
    }
    return Rect{};
}

bool NavigationShellControl::place_flyout(Element& content, Size client, const MeasureContext& context)
{
    const Rect  anchor  = flyout_anchor();
    const Size  natural = content.measure(Size{Unbounded, Unbounded}, context);
    const float width   = std::min(natural.width, client.width);
    float       x       = anchor.x;
    float       y       = anchor.bottom();
    float       height  = natural.height;
    if (flyout_kind_ == FlyoutKind::Overflow)
    {
        // Below the button, right edges aligned.
        x = anchor.right() - width;
    }
    else if (!top_mode())
    {
        // To the right of the row, top edges aligned and lifted 4.
        x = anchor.right();
        y = anchor.y - FlyoutCompactLift;
        if (x + width > client.width && anchor.x - width >= 0.0f)
        {
            x = anchor.x - width;
        }
    }
    else if (y + height > client.height && anchor.y - height >= 0.0f)
    {
        // Below the item, left edges aligned, flipping above when there is no room below.
        y = anchor.y - height;
    }
    x      = std::clamp(x, 0.0f, std::max(0.0f, client.width - width));
    y      = std::clamp(y, 0.0f, std::max(0.0f, client.height - std::min(height, client.height)));
    height = std::min(height, client.height - y);
    content.arrange(Rect{x, y, width, height});
    return true;
}

// ---- the pane's state ----

NavigationShellDisplayMode NavigationShellControl::resolve_display_mode(float width) const
{
    switch (pane_display_mode_)
    {
        case NavigationShellPaneDisplayMode::Left:
            return NavigationShellDisplayMode::Expanded;
        case NavigationShellPaneDisplayMode::LeftCompact:
            return NavigationShellDisplayMode::Compact;
        case NavigationShellPaneDisplayMode::LeftMinimal:
        case NavigationShellPaneDisplayMode::Top:
            return NavigationShellDisplayMode::Minimal;
        case NavigationShellPaneDisplayMode::Auto:
            break;
    }
    if (std::isfinite(width) && width >= expanded_threshold_) // unbounded measures as Compact
    {
        return NavigationShellDisplayMode::Expanded;
    }
    if (width > 0.0f && width < compact_threshold_)
    {
        return NavigationShellDisplayMode::Minimal;
    }
    return NavigationShellDisplayMode::Compact;
}

bool NavigationShellControl::overlay_mode() const
{
    return !top_mode() && display_mode_ != NavigationShellDisplayMode::Expanded;
}

bool NavigationShellControl::minimal_mode() const
{
    // SidePane Overlay; a pane made invisible collapses to a CompactOverlay of width 0.
    return !top_mode() && display_mode_ == NavigationShellDisplayMode::Minimal;
}

bool NavigationShellControl::compact_strip() const
{
    return !top_mode() && pane_visible_ && display_mode_ != NavigationShellDisplayMode::Minimal;
}

bool NavigationShellControl::closed_compact() const
{
    return compact_strip() && !pane_open_;
}

bool NavigationShellControl::pane_live() const
{
    return !top_mode() && pane_visible_ && (compact_strip() || pane_open_ || pane_clock_ > 0.0);
}

float NavigationShellControl::pane_progress() const
{
    const CubicBezier& curve = overlay_transition_ ? NavigationShellOverlayCurve : NavigationShellInlineCurve;
    if (pane_open_)
    {
        return easing::evaluate(curve, pane_clock_);
    }
    return 1.0f - easing::evaluate(curve, 1.0 - pane_clock_);
}

bool NavigationShellControl::show_back_button() const
{
    // Auto shows the back button. A Minimal pane that is open shows the close button in its
    // place.
    if (back_visible_ == AutoVisibility::Collapsed)
    {
        return false;
    }
    return !(!top_mode() && display_mode_ == NavigationShellDisplayMode::Minimal && pane_open_);
}

bool NavigationShellControl::show_close_button() const
{
    if (back_visible_ == AutoVisibility::Collapsed || top_mode() || !pane_open_)
    {
        return false;
    }
    return pane_display_mode_ == NavigationShellPaneDisplayMode::LeftMinimal ||
           (pane_display_mode_ == NavigationShellPaneDisplayMode::Auto &&
            display_mode_ == NavigationShellDisplayMode::Minimal);
}

bool NavigationShellControl::show_header() const
{
    // `always_show_header` is ignored in a left Minimal pane.
    return !header_.empty() &&
           (always_show_header_ || (!top_mode() && display_mode_ == NavigationShellDisplayMode::Minimal));
}

void NavigationShellControl::request_pane_open(bool open, bool light_dismiss)
{
    if (top_mode() && open)
    {
        return;
    }
    if (open == pane_open_)
    {
        return;
    }
    if (open)
    {
        if (on_pane_opening_)
        {
            on_pane_opening_();
        }
    }
    else if (on_pane_closing_)
    {
        // Only a light dismiss can be canceled.
        const bool cancel = detail::canceled(on_pane_closing_());
        if (cancel && light_dismiss && !was_force_closed_)
        {
            return;
        }
    }
    pane_open_          = open;
    overlay_transition_ = overlay_mode();
    // A host driving tick_override() runs the transition from wherever the clock is (a reversal picks
    // up mid-way); with none, the pane lands at once.
    if (!ticking_)
    {
        pane_clock_ = open ? 1.0 : 0.0;
    }
    close_flyout();
    rebuild_visible_rows();
    update_focus_trap();
    sync_buttons();
    hovered_item_ = detail::NoIndex;
    pressed_item_ = detail::NoIndex;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (open && on_pane_opened_)
    {
        on_pane_opened_();
    }
    else if (!open && on_pane_closed_)
    {
        on_pane_closed_();
    }
}

void NavigationShellControl::set_pane_open(bool open)
{
    // A close through the API is a forced one, and an open clears it.
    was_force_closed_ = !open;
    request_pane_open(open, false);
}

void NavigationShellControl::set_pane_display_mode(NavigationShellPaneDisplayMode mode)
{
    if (mode == pane_display_mode_)
    {
        return;
    }
    const NavigationShellPaneDisplayMode old = pane_display_mode_;
    close_flyout();
    pane_display_mode_ = mode;
    if (top_mode())
    {
        // The top bar has no pane to open.
        request_pane_open(false, false);
        if (display_mode_ != NavigationShellDisplayMode::Minimal)
        {
            fire_display_mode(NavigationShellDisplayMode::Minimal);
        }
    }
    else if (pane_open_ && mode == NavigationShellPaneDisplayMode::LeftMinimal)
    {
        request_pane_open(false, false);
    }
    else if (!pane_open_ && old == NavigationShellPaneDisplayMode::LeftMinimal &&
             mode == NavigationShellPaneDisplayMode::Left)
    {
        request_pane_open(true, false);
    }
    adaptive_pending_ = true;
    rebuild_visible_rows();
    indicator_from_ = std::nullopt;
    sync_buttons();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::fire_display_mode(NavigationShellDisplayMode mode)
{
    display_mode_       = mode;
    display_mode_known_ = true;
    if (on_display_mode_changed_)
    {
        on_display_mode_changed_(mode);
    }
}

void NavigationShellControl::update_adaptive_layout(float width)
{
    // Run when the width changes or the pane display mode was set.
    if (top_mode())
    {
        if (!display_mode_known_)
        {
            fire_display_mode(NavigationShellDisplayMode::Minimal);
        }
        return;
    }
    const NavigationShellDisplayMode mode     = resolve_display_mode(width);
    const NavigationShellDisplayMode previous = display_mode_;
    const bool                       first    = !display_mode_known_;
    if (first && mode != NavigationShellDisplayMode::Expanded)
    {
        // A pane that loads Minimal or Compact starts closed.
        pane_open_  = false;
        pane_clock_ = 0.0;
    }
    if (first || mode != previous)
    {
        fire_display_mode(mode);
    }
    if (mode == NavigationShellDisplayMode::Expanded && pane_visible_ && !was_force_closed_)
    {
        request_pane_open(true, false);
    }
    if (!first && previous == NavigationShellDisplayMode::Expanded && mode == NavigationShellDisplayMode::Compact)
    {
        request_pane_open(false, false);
    }
    if (mode == NavigationShellDisplayMode::Minimal)
    {
        request_pane_open(false, false);
    }
    rebuild_visible_rows();
    update_focus_trap();
    sync_buttons();
}

void NavigationShellControl::set_pane_visible(bool visible)
{
    if (visible == pane_visible_)
    {
        return;
    }
    pane_visible_     = visible;
    adaptive_pending_ = true;
    rebuild_visible_rows();
    sync_buttons();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::set_pane_toggle_button_visible(bool visible)
{
    toggle_visible_ = visible;
    sync_buttons();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::set_pane_title(std::string title)
{
    pane_title_ = std::move(title);
    sync_buttons();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::set_back_button_visible(AutoVisibility visible)
{
    back_visible_ = visible;
    sync_buttons();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::set_back_enabled(bool enabled)
{
    back_enabled_ = enabled;
    sync_buttons();
    mark_dirty(DirtyFlags::Visual);
}

void NavigationShellControl::set_settings_visible(bool visible)
{
    if (visible == settings_visible_)
    {
        return;
    }
    settings_visible_ = visible;
    if (!visible)
    {
        if (selected_index_ == Settings)
        {
            selected_index_ = detail::NoIndex;
            indicator_from_ = std::nullopt;
        }
        if (cursor_index_ == Settings)
        {
            cursor_index_ = selected_index_;
        }
    }
    rebuild_visible_rows();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void NavigationShellControl::sync_buttons()
{
    back_button_->set_enabled(back_enabled_ && is_enabled());
    close_button_->set_enabled(is_enabled());
    toggle_button_->set_enabled(is_enabled());
    search_button_->set_enabled(is_enabled());
    // The toggle button's name and tooltip say what a click does next.
    const std::string& toggle_name = pane_open_ ? strings().navigation_shell.close_navigation
                                                : strings().navigation_shell.open_navigation;
    toggle_button_->set_accessibility(Accessibility{.name = toggle_name});
    set_tip_text(*toggle_tip_, toggle_name);
    toggle_content_->set_title(pane_title_);
}

void NavigationShellControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_item_ = detail::NoIndex;
        pressed_item_ = detail::NoIndex;
        close_flyout();
    }
    sync_buttons();
    mark_dirty(DirtyFlags::Visual);
}

void NavigationShellControl::focus_changed(bool /*focused*/)
{
    if (is_focused() && (cursor_index_ == detail::NoIndex || !cursor_stop(cursor_index_)))
    {
        cursor_index_ = selected_index_ != detail::NoIndex ? selected_index_ : detail::NoIndex;
    }
    mark_dirty(DirtyFlags::Visual);
}

bool NavigationShellControl::takes_focus_at(Point p) const
{
    if (top_mode())
    {
        return std::any_of(top_slots_.begin(),
                           top_slots_.end(),
                           [p](const TopSlot& slot) { return slot.rect.contains(p); });
    }
    return menu_bar_rect_.contains(p) || footer_bar_rect_.contains(p);
}

bool NavigationShellControl::focusable() const
{
    if (!is_enabled())
    {
        return false;
    }
    if (top_mode())
    {
        return !top_slots_.empty();
    }
    if (!pane_live())
    {
        return false;
    }
    for (const std::vector<Row>* rows : {&menu_rows_, &footer_rows_})
    {
        for (const Row& row : *rows)
        {
            if (cursor_stop(row.item))
            {
                return true;
            }
        }
    }
    return false;
}

// ---- focus trap (SidePane's, for an overlaying pane) ----

void NavigationShellControl::update_focus_trap()
{
    const bool wanted = light_dismissible();
    if (wanted && !trapping_)
    {
        pending_request_ = FocusRequest::Enter;
    }
    else if (!wanted)
    {
        if (trapping_)
        {
            trapping_        = false;
            pending_request_ = FocusRequest::Restore;
        }
        else if (pending_request_ == FocusRequest::Enter)
        {
            pending_request_ = FocusRequest::None;
        }
    }
}

FocusRequest NavigationShellControl::take_focus_request()
{
    const FocusRequest request = pending_request_;
    pending_request_           = FocusRequest::None;
    if (request == FocusRequest::Enter)
    {
        trapping_ = true;
    }
    return request;
}

// ---- scrolling ----

void NavigationShellControl::set_pane_scroll_offset(float offset)
{
    const float max_offset = std::max(0.0f, menu_content_height_ - menu_viewport_.height);
    const float clamped    = std::clamp(offset, 0.0f, max_offset);
    if (clamped == pane_scroll_offset_)
    {
        return;
    }
    pane_scroll_offset_ = clamped;
    mark_dirty(DirtyFlags::Visual);
}

void NavigationShellControl::clamp_scroll()
{
    pane_scroll_offset_ = std::clamp(pane_scroll_offset_, 0.0f, std::max(0.0f, menu_content_height_ - menu_viewport_.height));
    footer_scroll_offset_ = std::clamp(footer_scroll_offset_,
                                       0.0f,
                                       std::max(0.0f, footer_content_height_ - footer_viewport_.height));
}

float NavigationShellControl::row_height(const Row& row, bool compact) const
{
    if (row.item == Settings)
    {
        return item_row_height_;
    }
    switch (all_[row.item]->kind)
    {
        case NavEntryKind::Header:
            // A section header is 0 tall while the pane is closed compact.
            return compact ? 0.0f : HeaderRowHeight;
        case NavEntryKind::Separator:
            return SeparatorRowHeight;
        case NavEntryKind::Item:
            break;
    }
    return item_row_height_;
}

float NavigationShellControl::rows_height(const std::vector<Row>& rows, bool compact) const
{
    float height = 0.0f;
    for (const Row& row : rows)
    {
        height += row_height(row, compact);
    }
    return height;
}

void NavigationShellControl::scroll_item_into_view(std::size_t index)
{
    if (top_mode())
    {
        return;
    }
    const bool compact = closed_compact();
    for (int list = 0; list < 2; ++list)
    {
        const std::vector<Row>& rows     = list == 0 ? menu_rows_ : footer_rows_;
        const Rect&             viewport = list == 0 ? menu_viewport_ : footer_viewport_;
        float&                  offset   = list == 0 ? pane_scroll_offset_ : footer_scroll_offset_;
        float                   top      = 0.0f;
        for (const Row& row : rows)
        {
            const float h = row_height(row, compact);
            if (row.item == index)
            {
                if (top < offset)
                {
                    offset = top;
                }
                else if (top + h > offset + viewport.height)
                {
                    offset = top + h - viewport.height;
                }
                clamp_scroll();
                mark_dirty(DirtyFlags::Visual);
                return;
            }
            top += h;
        }
    }
}

// ---- geometry queries ----

Rect NavigationShellControl::pane_bounds() const
{
    if (!pane_live())
    {
        return Rect{bounds().x, bounds().y, 0.0f, 0.0f};
    }
    return Rect{bounds().x, bounds().y, pane_visible_width_, bounds().height};
}

std::optional<Rect> NavigationShellControl::item_bounds(std::size_t index) const
{
    if (top_mode())
    {
        for (const TopSlot& slot : top_slots_)
        {
            if (slot.item == index)
            {
                return slot.rect;
            }
        }
        return std::nullopt;
    }
    if (!pane_live())
    {
        return std::nullopt;
    }
    const bool compact = closed_compact();
    for (int list = 0; list < 2; ++list)
    {
        const std::vector<Row>& rows     = list == 0 ? menu_rows_ : footer_rows_;
        const Rect&             viewport = list == 0 ? menu_viewport_ : footer_viewport_;
        const float             offset   = list == 0 ? pane_scroll_offset_ : footer_scroll_offset_;
        float                   y        = viewport.y - offset;
        for (const Row& row : rows)
        {
            const float h = row_height(row, compact);
            if (row.item == index)
            {
                if (y + h <= viewport.y || y >= viewport.bottom())
                {
                    return std::nullopt;
                }
                return inset_item_row(Rect{viewport.x, y, viewport.width, h});
            }
            y += h;
        }
    }
    return std::nullopt;
}

std::optional<Rect> NavigationShellControl::overflow_button_bounds() const
{
    for (const TopSlot& slot : top_slots_)
    {
        if (slot.item == detail::NoIndex)
        {
            return slot.rect;
        }
    }
    return std::nullopt;
}

bool NavigationShellControl::in_overflow(std::size_t index) const
{
    std::size_t top = index;
    while (top < all_.size() && parent_of(top) != detail::NoIndex)
    {
        top = parent_of(top);
    }
    return std::find(overflow_items_.begin(), overflow_items_.end(), top) != overflow_items_.end();
}

// ---- layout ----

float NavigationShellControl::top_item_width(std::size_t index) const
{
    if (index == Settings)
    {
        return TopSettingsWidth;
    }
    return index < top_widths_.size() ? top_widths_[index] : 0.0f;
}

Size NavigationShellControl::measure_override(Size available, const MeasureContext& context) const
{
    // An era's pane bars take a gutter from the lists, which layout_left() gives them.
    scrollbar_.apply_theme(context.theme);
    footer_scrollbar_.apply_theme(context.theme);
    const float width  = available.width;
    const float height = available.height;

    badge_sizes_.assign(all_.size(), Size{});
    for (std::size_t i = 0; i < all_.size(); ++i)
    {
        if (InfoBadgeControl* badge = all_[i]->info_badge.get(); badge != nullptr)
        {
            badge_sizes_[i] = badge->measure(Size{Unbounded, Unbounded}, context);
        }
    }

    // Each item's width in the top bar (layout_top() works from these).
    top_widths_.assign(all_.size(), 0.0f);
    const TextStyle label  = item_label_style(context.theme);
    const TextStyle header = header_item_style(context.theme);
    for (std::size_t i = 0; i < all_.size(); ++i)
    {
        const NavEntry& e = *all_[i];
        switch (e.kind)
        {
            case NavEntryKind::Separator:
                top_widths_[i] = TopSeparatorWidth;
                continue;
            case NavEntryKind::Header:
                top_widths_[i] = context.measurer.measure(e.text, header).width + 2.0f * HeaderInsetTop;
                continue;
            case NavEntryKind::Item:
                break;
        }
        const bool  has_label = !e.text.empty();
        const bool  has_icon  = !e.icon.empty() || e.animated_icon;
        const float text      = has_label ? std::ceil(context.measurer.measure(e.text, label).width) : 0.0f;
        float       w         = 0.0f;
        if (has_icon && has_label)
        {
            w = TopIconLead + NavigationShellIconSize + TopIconGap + text + TopLabelTrail + 2.0f * ItemMarginH;
        }
        else if (has_label)
        {
            w = TopIconLead + text + TopLabelTrail + 2.0f * ItemMarginH;
        }
        else
        {
            w = TopIconOnlySlot;
        }
        if (!e.children.empty())
        {
            w += has_icon && has_label ? ChevronBoxWidth - 16.0f
                 : has_label           ? ChevronBoxWidth - 12.0f
                                       : ChevronBoxWidth;
        }
        if (e.info_badge)
        {
            w += std::max(0.0f, badge_sizes_[i].width - 16.0f + 2.0f);
        }
        top_widths_[i] = w;
    }

    // The header's text box, in the title style.
    TextStyle header_style = context.theme.type.title;
    header_text_height_    = header_.empty() ? 0.0f : context.measurer.measure(header_, header_style).height;
    title_width_ = pane_title_.empty() ? 0.0f : std::ceil(context.measurer.measure(pane_title_, header).width);

    const Size button{NavigationShellButtonWidth, NavigationShellButtonHeight};
    for (ToolTipControl* tip : {back_tip_.get(), close_tip_.get(), toggle_tip_.get()})
    {
        (void)tip->measure(button, context);
    }
    (void)search_button_->measure(button, context);
    (void)row_tip_->measure(Size{Unbounded, Unbounded}, context);

    const bool  top    = top_mode();
    const float pane_w = std::min(pane_width_, width);
    asb_desired_ = auto_suggest_box_
                       ? auto_suggest_box_->measure(top ? Size{TopAsbMinWidth, TopBarHeight}
                                                        : Size{std::max(0.0f, pane_w - 2.0f * AsbInset), Unbounded},
                                                    context)
                       : Size{};
    pane_header_desired_ = pane_header_
                               ? pane_header_->measure(top ? Size{Unbounded, TopBarHeight} : Size{pane_w, Unbounded}, context)
                               : Size{};
    custom_desired_ = pane_custom_content_
                          ? pane_custom_content_->measure(top ? Size{Unbounded, TopBarHeight} : Size{pane_w, Unbounded}, context)
                          : Size{};
    pane_footer_desired_ = pane_footer_
                               ? pane_footer_->measure(top ? Size{Unbounded, TopBarHeight} : Size{pane_w, Unbounded}, context)
                               : Size{};
    // The page, against what the pane and the header leave of the room.
    const NavigationShellDisplayMode mode   = display_mode_known_ ? display_mode_ : resolve_display_mode(width);
    float                            column = 0.0f;
    if (!top && pane_visible_)
    {
        if (mode == NavigationShellDisplayMode::Expanded)
        {
            column = pane_open_ ? pane_w : compact_pane_length_;
        }
        else if (mode == NavigationShellDisplayMode::Compact)
        {
            column = compact_pane_length_;
        }
    }
    const float header_h = show_header() ? HeaderTop + std::max(NavigationShellHeaderMinHeight, header_text_height_) : 0.0f;
    const float top_h = top ? TopBarHeight : 0.0f;
    const Size content_available{std::max(0.0f, width - column - 1.0f), std::max(0.0f, height - top_h - header_h - 1.0f)};
    Size content_desired{};
    if (Element* content = active_content(); content != nullptr)
    {
        content_desired = content->measure(content_available, context);
    }

    // The pane's own height, row by row as layout_left() stacks them.
    const bool minimal   = mode == NavigationShellDisplayMode::Minimal;
    const bool toggle    = toggle_visible_ && pane_visible_;
    const bool compact   = !top && pane_visible_ && !minimal && !pane_open_;
    float      pane_rows = PaneTopInset + PaneBottomInset + FooterBottom;
    pane_rows += !minimal && back_visible_ != AutoVisibility::Collapsed ? BackRowHeight : 0.0f;
    pane_rows += std::max(toggle || (!pane_title_.empty() && pane_open_) ? ToggleRowHeight : 0.0f,
                          pane_header_ && !compact ? pane_header_desired_.height : 0.0f);
    pane_rows += auto_suggest_box_ ? std::max(AsbRowHeight, asb_desired_.height) + AsbRowBottom : 0.0f;
    pane_rows += custom_desired_.height + pane_footer_desired_.height;
    pane_rows += rows_height(menu_rows_, compact) + rows_height(footer_rows_, compact);
    Size desired{column + content_desired.width + 1.0f,
                 std::max(top ? top_h + header_h + content_desired.height : pane_rows,
                          header_h + content_desired.height + 1.0f)};
    if (std::isfinite(width))
    {
        desired.width = width;
    }
    if (std::isfinite(height))
    {
        desired.height = height;
    }
    return desired;
}

void NavigationShellControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (adaptive_pending_ || final_rect.width != adaptive_width_)
    {
        adaptive_pending_ = false;
        adaptive_width_   = final_rect.width;
        update_adaptive_layout(final_rect.width);
    }
    rebuild_visible_rows();
    if (top_mode())
    {
        layout_top(final_rect);
    }
    else
    {
        layout_left(final_rect);
    }
    if (reveal_pending_ != detail::NoIndex)
    {
        scroll_item_into_view(reveal_pending_);
        reveal_pending_ = detail::NoIndex;
    }
    if (Element* content = active_content(); content != nullptr)
    {
        content->arrange(page_rect_);
    }
    sync_popup(); // Top and Left mode place the flyout differently
}

void NavigationShellControl::place(Element* element, const Rect& rect)
{
    if (element == nullptr)
    {
        return;
    }
    if (rect.width <= 0.0f || rect.height <= 0.0f)
    {
        element->arrange(Rect{rect.x, rect.y, 0.0f, 0.0f});
        return;
    }
    element->arrange(rect);
}

void NavigationShellControl::layout_content(const Rect& grid)
{
    content_rect_ = grid;
    // The content's border: 1,1,0,0 beside a pane, 0,1,0,0 under the top bar and in Minimal.
    const bool  side_border = !top_mode() && display_mode_ != NavigationShellDisplayMode::Minimal && pane_visible_;
    const float left        = side_border ? 1.0f : 0.0f;
    const Rect inner{grid.x + left, grid.y + 1.0f, std::max(0.0f, grid.width - left), std::max(0.0f, grid.height - 1.0f)};
    float page_top = inner.y;
    header_rect_   = Rect{inner.x, inner.y, 0.0f, 0.0f};
    if (show_header())
    {
        float x = inner.x + HeaderLeft;
        if (minimal_mode())
        {
            // The content's left padding: the toggle and back/close buttons' widths, then the
            // Minimal margin.
            float padding = toggle_visible_ ? NavigationShellButtonWidth : 0.0f;
            padding += show_back_button() ? BackColumnWidth : 0.0f;
            padding += show_close_button() ? BackColumnWidth : 0.0f;
            x = inner.x + padding + HeaderLeftMinimal;
        }
        const float h = std::max(NavigationShellHeaderMinHeight, header_text_height_);
        header_rect_  = Rect{x, inner.y + HeaderTop, std::max(0.0f, inner.right() - x), h};
        page_top      = header_rect_.bottom();
    }
    page_rect_ = Rect{inner.x, page_top, inner.width, std::max(0.0f, inner.bottom() - page_top)};
}

void NavigationShellControl::layout_left(const Rect& r)
{
    top_slots_.clear();
    overflow_items_.clear();
    top_bar_rect_ = Rect{};

    const float open_len    = std::min(pane_width_, r.width);
    const float compact_len = std::min(compact_pane_length_, open_len);
    const bool  compact     = closed_compact();

    // The content's column (SidePane's): an inline pane pushes the content over, a compact
    // overlay leaves the strip, Minimal leaves nothing.
    float column = 0.0f;
    if (pane_visible_)
    {
        if (display_mode_ == NavigationShellDisplayMode::Expanded)
        {
            column = pane_open_ ? open_len : compact_len;
        }
        else if (display_mode_ == NavigationShellDisplayMode::Compact)
        {
            column = compact_len;
        }
    }
    layout_content(Rect{r.x + column, r.y, std::max(0.0f, r.width - column), r.height});

    // Minimal has no strip, so its pane slides in instead of being uncovered
    // (SidePane.cpp's place_children()).
    pane_rect_               = Rect{r.x, r.y, open_len, r.height};
    const float closed_width = compact_strip() ? compact_len : 0.0f;
    pane_visible_width_      = pane_live() ? closed_width + (open_len - closed_width) * pane_progress() : 0.0f;
    pane_overlaying_   = pane_live() && overlay_mode() && (pane_open_ || pane_visible_width_ > closed_width + 0.5f);
    const float pane_x = minimal_mode() ? r.x - (open_len - pane_visible_width_) : r.x;
    // A closed compact pane's list is the strip's width.
    const float list_w = compact ? compact_len : open_len;

    // ---- the buttons, over everything at the top-left ----
    const float by     = r.y + ButtonHolderTop;
    const bool  back   = show_back_button();
    const bool  close  = show_close_button();
    const bool  toggle = toggle_visible_ && pane_visible_;
    const Rect  back_rect{r.x + ItemMarginH, by + ItemMarginV, NavigationShellButtonWidth, NavigationShellButtonHeight};
    place(back_tip_.get(), back ? back_rect : Rect{r.x, r.y, 0.0f, 0.0f});
    place(close_tip_.get(), close ? back_rect : Rect{r.x, r.y, 0.0f, 0.0f});
    if (toggle)
    {
        // The toggle button's margin and width.
        const float margin_left = minimal_mode() && (back || close) ? BackColumnWidth : 0.0f;
        const float margin_top  = !minimal_mode() && back ? BackRowHeight : 0.0f;
        float       width       = compact_pane_length_;
        if (!compact && !pane_title_.empty())
        {
            if (minimal_mode() && pane_open_)
            {
                width = open_len - (back || close ? BackColumnWidth : 0.0f);
            }
            else if (!(minimal_mode() && !pane_open_))
            {
                width = open_len;
            }
        }
        place(toggle_tip_.get(),
              Rect{r.x + margin_left + ItemMarginH,
                   by + margin_top + ItemMarginV,
                   std::max(NavigationShellButtonWidth, width - 2.0f * ItemMarginH),
                   NavigationShellButtonHeight});
    }
    else
    {
        place(toggle_tip_.get(), Rect{r.x, r.y, 0.0f, 0.0f});
    }

    // ---- the pane's rows ----
    float y = r.y + PaneTopInset;
    y += !minimal_mode() && back ? BackRowHeight : 0.0f;

    // The pane header, beside the toggle (and close) button in the row the toggle button sets.
    const bool  title_row  = toggle || (!pane_title_.empty() && pane_open_);
    const float header_col = (toggle ? NavigationShellButtonWidth : 0.0f) +
                             (close && minimal_mode() ? BackColumnWidth : 0.0f);
    float       row2       = title_row ? ToggleRowHeight : 0.0f;
    if (pane_header_ && !compact)
    {
        row2 = std::max(row2, pane_header_desired_.height);
        place(pane_header_.get(), Rect{pane_x + header_col, y, std::max(0.0f, list_w - header_col), row2});
    }
    else if (pane_header_)
    {
        place(pane_header_.get(), Rect{pane_x, y, 0.0f, 0.0f});
    }
    y += row2;

    // The AutoSuggestBox's area, or the search button in its place while closed compact.
    place(search_button_, Rect{r.x, r.y, 0.0f, 0.0f});
    if (auto_suggest_box_)
    {
        const float row = std::max(AsbRowHeight, compact ? NavigationShellButtonHeight : asb_desired_.height);
        if (compact)
        {
            place(search_button_,
                  Rect{pane_x + ItemMarginH,
                       y + (row - NavigationShellButtonHeight) * 0.5f,
                       NavigationShellButtonWidth,
                       NavigationShellButtonHeight});
            place(auto_suggest_box_.get(), Rect{pane_x, y, 0.0f, 0.0f});
        }
        else
        {
            place(auto_suggest_box_.get(),
                  Rect{pane_x + AsbInset,
                       y + (row - asb_desired_.height) * 0.5f,
                       std::max(0.0f, list_w - 2.0f * AsbInset),
                       asb_desired_.height});
        }
        y += row + AsbRowBottom;
    }

    // The custom pane content (left-aligned at its own width while closed compact).
    if (pane_custom_content_)
    {
        const float w = compact ? std::min(custom_desired_.width, list_w) : list_w;
        place(pane_custom_content_.get(), Rect{pane_x, y, w, custom_desired_.height});
        y += custom_desired_.height;
    }

    // The menu items, the separator, the pane footer and the footer items share what is left.
    const float bottom        = r.bottom() - PaneBottomInset;
    const float available     = std::max(0.0f, bottom - y);
    const float pane_footer_h = (pane_footer_ ? pane_footer_desired_.height : 0.0f) + FooterBottom;
    footer_content_height_    = rows_height(footer_rows_, compact);
    menu_content_height_      = rows_height(menu_rows_, compact);
    const float footer_group  = footer_content_height_ + pane_footer_h;
    const float half          = available * 0.5f;
    float       footer_max    = 0.0f;
    separator_visible_        = false;
    if (footer_rows_.empty())
    {
        footer_max = 0.0f;
    }
    else if (menu_rows_.empty())
    {
        footer_max = available;
    }
    else if (available >= menu_content_height_ + footer_group)
    {
        footer_max = footer_content_height_;
    }
    else if (menu_content_height_ <= half)
    {
        footer_max         = available - menu_content_height_;
        separator_visible_ = true;
    }
    else if (footer_group <= half)
    {
        footer_max         = footer_content_height_;
        separator_visible_ = true;
    }
    else
    {
        footer_max         = half;
        separator_visible_ = true;
    }
    const float footer_h = std::max(0.0f, std::min(footer_content_height_, footer_max));
    const float sep_h    = separator_visible_ ? PaneSeparatorHeight : 0.0f;
    const float menu_h   = std::max(0.0f, available - sep_h - pane_footer_h - footer_h);

    menu_viewport_    = Rect{pane_x, y, list_w, menu_h};
    separator_rect_   = Rect{pane_x, y + menu_h, list_w, sep_h};
    pane_footer_rect_ = Rect{pane_x, y + menu_h + sep_h, list_w, pane_footer_h - FooterBottom};
    footer_viewport_  = Rect{pane_x, bottom - footer_h, list_w, footer_h};
    // An era's bar takes a gutter beside the rows of a list that overflows.
    menu_bar_rect_   = menu_viewport_;
    footer_bar_rect_ = footer_viewport_;
    if (const float gutter = scrollbar_.gutter(); gutter > 0.0f)
    {
        if (menu_content_height_ > menu_viewport_.height)
        {
            menu_viewport_.width = std::max(0.0f, menu_viewport_.width - gutter);
        }
        if (footer_content_height_ > footer_viewport_.height)
        {
            footer_viewport_.width = std::max(0.0f, footer_viewport_.width - gutter);
        }
    }
    if (pane_footer_)
    {
        const float w = compact ? std::min(pane_footer_desired_.width, list_w) : list_w;
        place(pane_footer_.get(), Rect{pane_footer_rect_.x, pane_footer_rect_.y, w, pane_footer_rect_.height});
    }
    clamp_scroll();
}

void NavigationShellControl::layout_top(const Rect& r)
{
    top_slots_.clear();
    overflow_items_.clear();
    pane_visible_width_ = 0.0f;
    pane_overlaying_    = false;
    pane_rect_          = Rect{};
    menu_viewport_ = footer_viewport_ = separator_rect_ = pane_footer_rect_ = Rect{};
    menu_bar_rect_ = footer_bar_rect_ = Rect{};
    separator_visible_                = false;
    top_bar_rect_                     = Rect{r.x, r.y, r.width, std::min(TopBarHeight, r.height)};
    layout_content(Rect{r.x, r.y + top_bar_rect_.height, r.width, std::max(0.0f, r.height - top_bar_rect_.height)});

    const Rect none{r.x, r.y, 0.0f, 0.0f};
    place(toggle_tip_.get(), none);
    place(close_tip_.get(), none);
    place(search_button_, none);

    const float bar_y = r.y;
    float       x     = r.x + TopBarInset;
    float       right = r.right() - TopBarInset;

    // Column 0: the back button's placeholder, the button itself centered in the bar.
    const bool back = show_back_button();
    place(back_tip_.get(),
          back ? Rect{r.x + ItemMarginH, bar_y + ButtonHolderTop + ItemMarginV, NavigationShellButtonWidth, NavigationShellButtonHeight}
               : none);
    x += back ? BackColumnWidth : 0.0f;

    // The pane header, then the pane title (only while the toggle button is hidden).
    if (pane_header_)
    {
        place(pane_header_.get(), Rect{x, bar_y, pane_header_desired_.width, TopBarHeight});
        x += pane_header_desired_.width;
    }
    title_rect_ = Rect{};
    if (!toggle_visible_ && !pane_title_.empty())
    {
        title_rect_ = Rect{x + HeaderInsetLeft, bar_y, title_width_, TopBarHeight};
        x += title_width_ + 2.0f * HeaderInsetLeft;
    }

    // From the right: the footer items and Settings, the pane footer, the AutoSuggestBox.
    std::vector<TopSlot> right_slots;
    {
        float rx = right;
        for (auto it = footer_rows_.rbegin(); it != footer_rows_.rend(); ++it)
        {
            const float w = top_item_width(it->item);
            rx -= w;
            right_slots.insert(right_slots.begin(), TopSlot{it->item, Rect{rx, bar_y, w, TopBarHeight}});
        }
        right = rx;
    }
    if (pane_footer_)
    {
        right -= pane_footer_desired_.width;
        place(pane_footer_.get(), Rect{right, bar_y, pane_footer_desired_.width, TopBarHeight});
    }
    if (auto_suggest_box_)
    {
        const float w = TopAsbMinWidth;
        right -= w + 2.0f * TopAsbMargin;
        place(auto_suggest_box_.get(),
              Rect{right + TopAsbMargin,
                   bar_y + (TopBarHeight - asb_desired_.height) * 0.5f,
                   w,
                   std::min(asb_desired_.height, TopBarHeight)});
    }

    // The primary items: everything if it fits, else the
    // overflow button and as long a prefix as fits beside the selected item, which always stays.
    std::vector<std::size_t> candidates;
    candidates.reserve(menu_rows_.size());
    for (const Row& row : menu_rows_)
    {
        candidates.push_back(row.item);
    }
    std::size_t selected_top = selected_index_;
    while (selected_top < all_.size() && parent_of(selected_top) != detail::NoIndex)
    {
        selected_top = parent_of(selected_top);
    }
    const float budget = std::max(0.0f, right - TopCustomMinWidth - x);
    float       total  = 0.0f;
    for (std::size_t item : candidates)
    {
        total += top_item_width(item);
    }
    std::vector<std::size_t> primary;
    if (total < budget || candidates.empty())
    {
        primary = candidates;
    }
    else
    {
        const bool has_selected = std::find(candidates.begin(), candidates.end(), selected_top) != candidates.end();
        float      remaining    = budget - OverflowButtonSize - (has_selected ? top_item_width(selected_top) : 0.0f);
        bool       spilled      = false;
        std::vector<bool> keep(candidates.size(), false);
        for (std::size_t k = 0; k < candidates.size(); ++k)
        {
            if (candidates[k] == selected_top)
            {
                keep[k] = true;
                continue;
            }
            const float w = top_item_width(candidates[k]);
            if (!spilled && w <= remaining)
            {
                keep[k] = true;
                remaining -= w;
            }
            else
            {
                spilled = true;
            }
        }
        // At least one item stays in the bar.
        if (std::find(keep.begin(), keep.end(), true) == keep.end())
        {
            keep.front() = true;
        }
        for (std::size_t k = 0; k < candidates.size(); ++k)
        {
            (keep[k] ? primary : overflow_items_).push_back(candidates[k]);
        }
    }
    for (std::size_t item : primary)
    {
        const float w = top_item_width(item);
        top_slots_.push_back(TopSlot{item, Rect{x, bar_y, w, TopBarHeight}});
        x += w;
    }
    if (!overflow_items_.empty())
    {
        top_slots_.push_back(
            TopSlot{detail::NoIndex,
                    Rect{x, bar_y + (TopBarHeight - OverflowButtonSize) * 0.5f, OverflowButtonSize, OverflowButtonSize}});
        x += OverflowButtonSize;
    }
    // The custom pane content fills the column between.
    if (pane_custom_content_)
    {
        place(pane_custom_content_.get(), Rect{x, bar_y, std::max(0.0f, right - x), TopBarHeight});
    }
    for (const TopSlot& slot : right_slots)
    {
        top_slots_.push_back(slot);
    }
    if (flyout_kind_ == FlyoutKind::Overflow)
    {
        if (overflow_items_.empty())
        {
            close_flyout();
        }
        else
        {
            rebuild_flyout_rows();
        }
    }
}

// ---- hit testing ----

NavigationShellControl::Hit NavigationShellControl::hit_test(Point p) const
{
    Hit hit;
    if (top_mode())
    {
        for (const TopSlot& slot : top_slots_)
        {
            if (!slot.rect.contains(p))
            {
                continue;
            }
            if (slot.item == detail::NoIndex)
            {
                hit.overflow_button = true;
                return hit;
            }
            if (slot.item != Settings && all_[slot.item]->kind != NavEntryKind::Item)
            {
                return hit;
            }
            hit.item = slot.item;
            return hit;
        }
        return hit;
    }
    if (!pane_live() || !pane_bounds().contains(p))
    {
        return hit;
    }
    const bool compact = closed_compact();
    for (int list = 0; list < 2; ++list)
    {
        const std::vector<Row>& rows     = list == 0 ? menu_rows_ : footer_rows_;
        const Rect&             viewport = list == 0 ? menu_viewport_ : footer_viewport_;
        const float             offset   = list == 0 ? pane_scroll_offset_ : footer_scroll_offset_;
        if (!viewport.contains(p))
        {
            continue;
        }
        float y = viewport.y - offset;
        for (const Row& row : rows)
        {
            const float h = row_height(row, compact);
            if (p.y >= y && p.y < y + h)
            {
                if (!cursor_stop(row.item))
                {
                    return hit;
                }
                hit.item         = row.item;
                const Rect pill  = inset_item_row(Rect{viewport.x, y, viewport.width, h});
                const bool group = row.item != Settings && !all_[row.item]->children.empty();
                hit.chevron = group && !(compact && row.depth == 0) &&
                              Rect{pill.right() - ChevronBoxWidth, pill.y, ChevronBoxWidth, pill.height}.contains(p);
                return hit;
            }
            y += h;
        }
    }
    return hit;
}

// ---- keyboard ----

bool NavigationShellControl::takes_arrow_key(Key key) const
{
    if (flyout_open())
    {
        return true;
    }
    if (top_mode())
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    if (key == Key::ArrowUp || key == Key::ArrowDown)
    {
        return true;
    }
    return (key == Key::ArrowLeft || key == Key::ArrowRight) && has_groups_ && !closed_compact();
}

void NavigationShellControl::move_cursor(int delta)
{
    // The stops in drawn order: menu rows, then the footer rows (the top bar's slots in Top mode).
    std::vector<std::size_t> stops;
    if (top_mode())
    {
        for (const TopSlot& slot : top_slots_)
        {
            if (slot.item == detail::NoIndex)
            {
                stops.push_back(OverflowStop);
            }
            else if (cursor_stop(slot.item))
            {
                stops.push_back(slot.item);
            }
        }
    }
    else
    {
        for (const std::vector<Row>* rows : {&menu_rows_, &footer_rows_})
        {
            for (const Row& row : *rows)
            {
                if (cursor_stop(row.item))
                {
                    stops.push_back(row.item);
                }
            }
        }
    }
    if (stops.empty())
    {
        return;
    }
    auto        it   = std::find(stops.begin(), stops.end(), cursor_index_);
    std::size_t next = 0;
    if (it != stops.end())
    {
        const auto pos = static_cast<std::ptrdiff_t>(it - stops.begin());
        // Nothing wraps: an arrow with nothing that way leaves the cursor put.
        if (delta < 0 && pos == 0)
        {
            return;
        }
        if (delta > 0 && pos + 1 >= static_cast<std::ptrdiff_t>(stops.size()))
        {
            return;
        }
        next = static_cast<std::size_t>(pos + (delta < 0 ? -1 : 1));
    }
    else if (delta < 0)
    {
        next = stops.size() - 1;
    }
    cursor_index_ = stops[next];
    if (cursor_index_ != OverflowStop)
    {
        scroll_item_into_view(cursor_index_);
        // With selection following focus, arriving selects.
        if (selection_follows_focus_ == NavigationShellSelectionFollowsFocus::Enabled)
        {
            select(cursor_index_);
        }
    }
    mark_dirty(DirtyFlags::Visual);
}

void NavigationShellControl::move_flyout_cursor(int delta)
{
    std::vector<std::size_t> stops;
    for (const Row& row : flyout_rows_)
    {
        if (cursor_stop(row.item))
        {
            stops.push_back(row.item);
        }
    }
    if (stops.empty())
    {
        return;
    }
    auto it = std::find(stops.begin(), stops.end(), flyout_cursor_);
    if (it == stops.end())
    {
        flyout_cursor_ = delta < 0 ? stops.back() : stops.front();
    }
    else
    {
        const auto pos  = static_cast<std::ptrdiff_t>(it - stops.begin());
        const auto next = std::clamp<std::ptrdiff_t>(pos + (delta < 0 ? -1 : 1),
                                                     0,
                                                     static_cast<std::ptrdiff_t>(stops.size()) - 1);
        flyout_cursor_  = stops[static_cast<std::size_t>(next)];
    }
    if (selection_follows_focus_ == NavigationShellSelectionFollowsFocus::Enabled)
    {
        select(flyout_cursor_);
    }
    mark_dirty(DirtyFlags::Visual);
}

bool NavigationShellControl::key_override(const KeyEvent& e)
{
    if (close_on_escape(e))
    {
        return true;
    }
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }

    if (flyout_open())
    {
        switch (e.key)
        {
            case Key::ArrowUp:
                move_flyout_cursor(-1);
                return handled;
            case Key::ArrowDown:
                move_flyout_cursor(+1);
                return handled;
            case Key::Enter:
            case Key::Space:
                if (flyout_cursor_ != detail::NoIndex)
                {
                    const std::size_t item = flyout_cursor_;
                    invoke(item, false, true);
                }
                return handled;
            case Key::ArrowLeft:
                if (!top_mode())
                {
                    close_flyout();
                }
                return handled;
            case Key::Escape:
                close_flyout();
                return handled;
            default:
                return handled;
        }
    }

    const std::size_t cursor = cursor_index_;
    const bool        top    = top_mode();
    switch (e.key)
    {
        case Key::ArrowUp:
            if (!top)
            {
                move_cursor(-1);
            }
            break;
        case Key::ArrowDown:
            if (!top)
            {
                move_cursor(+1);
            }
            else if (cursor == OverflowStop)
            {
                open_overflow_flyout();
                flyout_by_keyboard_ = true;
            }
            else if (cursor < all_.size() && !all_[cursor]->children.empty())
            {
                open_children_flyout(cursor);
                flyout_by_keyboard_ = true;
            }
            break;
        case Key::ArrowLeft:
            if (top)
            {
                move_cursor(-1);
            }
            else if (cursor < all_.size())
            {
                set_expanded(cursor, false);
            }
            break;
        case Key::ArrowRight:
            if (top)
            {
                move_cursor(+1);
            }
            else if (cursor < all_.size())
            {
                set_expanded(cursor, true);
            }
            break;
        case Key::Home:
        case Key::End:
        {
            // Home and End move the cursor to the first and last item.
            const int step = e.key == Key::Home ? -1 : +1;
            for (int guard = 0; guard < 1 << 16; ++guard)
            {
                const std::size_t before = cursor_index_;
                move_cursor(step);
                if (cursor_index_ == before)
                {
                    break;
                }
            }
            break;
        }
        case Key::Enter:
        case Key::Space:
            if (cursor == OverflowStop)
            {
                if (flyout_kind_ == FlyoutKind::Overflow)
                {
                    close_flyout();
                }
                else
                {
                    open_overflow_flyout();
                    flyout_by_keyboard_ = true;
                }
            }
            else if (cursor != detail::NoIndex && cursor_stop(cursor))
            {
                invoke(cursor, false, false);
                if (flyout_open())
                {
                    flyout_by_keyboard_ = true;
                }
            }
            break;
        default:
            break;
    }
    return handled;
}

bool NavigationShellControl::close_on_escape(const KeyEvent& e)
{
    // Escape closes a light-dismissable pane.
    if (e.key == Key::Escape && e.action == KeyAction::Down && light_dismissible())
    {
        request_pane_open(false, true);
        return true;
    }
    return false;
}

// ---- pointer ----

bool NavigationShellControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    const bool left_pane   = !top_mode();
    const auto over_button = [&](Point p)
    {
        for (const Element* el : {static_cast<const Element*>(back_tip_.get()),
                                  static_cast<const Element*>(close_tip_.get()),
                                  static_cast<const Element*>(toggle_tip_.get())})
        {
            if (el->bounds().width > 0.0f && el->bounds().contains(p))
            {
                return true;
            }
        }
        return false;
    };
    if ((e.action == PointerAction::Down || e.action == PointerAction::Up) && over_button(e.position))
    {
        return true; // the buttons' own, bubbling up
    }

    const Rect pane    = pane_bounds();
    const bool in_pane = left_pane && pane_live() && pane.contains(e.position);

    // The overlay scrollbars get first refusal over their own bands, as in Scroll.
    if (in_pane && e.action != PointerAction::Wheel)
    {
        float offset = pane_scroll_offset_;
        if (menu_content_height_ > menu_viewport_.height &&
            scrollbar_.handle_pointer(e, menu_bar_rect_, menu_content_height_, offset))
        {
            set_pane_scroll_offset(offset);
            return true;
        }
        float footer_offset = footer_scroll_offset_;
        if (footer_content_height_ > footer_viewport_.height &&
            footer_scrollbar_.handle_pointer(e, footer_bar_rect_, footer_content_height_, footer_offset))
        {
            footer_scroll_offset_ = footer_offset;
            clamp_scroll();
            mark_dirty(DirtyFlags::Visual);
            return true;
        }
    }

    const Hit hit      = hit_test(e.position);
    bool      on_items = hit.item != detail::NoIndex || hit.overflow_button;
    switch (e.action)
    {
        case PointerAction::Move:
        {
            const std::size_t hovered = hit.item;
            if (hovered != hovered_item_ || hit.chevron != hovered_chevron_ || hit.overflow_button != overflow_hovered_)
            {
                hovered_item_     = hovered;
                hovered_chevron_  = hit.chevron;
                overflow_hovered_ = hit.overflow_button;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        }
        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                if (hit.overflow_button)
                {
                    overflow_pressed_ = true;
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                if (hit.item != detail::NoIndex)
                {
                    // Invocation happens on Down, so the Pressed look lasts until the button
                    // comes back up.
                    pressed_item_ = hit.item;
                    row_tip_->close();
                    invoke(hit.item, hit.chevron, false);
                    return true;
                }
            }
            break;
        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                if (overflow_pressed_)
                {
                    overflow_pressed_ = false;
                    if (hit.overflow_button)
                    {
                        if (flyout_kind_ == FlyoutKind::Overflow)
                        {
                            close_flyout();
                        }
                        else
                        {
                            open_overflow_flyout();
                        }
                    }
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                if (pressed_item_ != detail::NoIndex)
                {
                    pressed_item_ = detail::NoIndex;
                    mark_dirty(DirtyFlags::Visual);
                }
            }
            break;
        case PointerAction::Leave:
            hovered_item_     = detail::NoIndex;
            pressed_item_     = detail::NoIndex;
            hovered_chevron_  = false;
            overflow_hovered_ = false;
            overflow_pressed_ = false;
            mark_dirty(DirtyFlags::Visual);
            break;
        default:
            break;
    }

    // A row's tooltip: its label only while closed compact, an explicit tooltip anywhere.
    std::string tip_text;
    if (hit.item == Settings)
    {
        tip_text = top_mode() || !pane_open_ ? strings().navigation_shell.settings : std::string{};
    }
    else if (hit.item < all_.size())
    {
        const NavEntry& en = *all_[hit.item];
        tip_text = !en.tool_tip.empty() ? en.tool_tip
                                        : (closed_compact() && depth_of(hit.item) == 0 ? en.text : std::string{});
    }
    else if (hit.overflow_button)
    {
        tip_text = strings().navigation_shell.more;
    }
    const std::size_t tip_key = hit.overflow_button ? OverflowStop : hit.item;
    if (tip_key != row_tip_item_ || tip_text.empty())
    {
        row_tip_->close();
        row_tip_item_ = tip_text.empty() ? detail::NoIndex : tip_key;
        if (!tip_text.empty())
        {
            set_tip_text(*row_tip_, tip_text);
            std::optional<Rect> rect = hit.overflow_button ? overflow_button_bounds() : item_bounds(hit.item);
            row_tip_->arrange(rect.value_or(Rect{}));
        }
        else
        {
            row_tip_->arrange(Rect{bounds().x, bounds().y, 0.0f, 0.0f});
        }
    }
    if (row_tip_item_ != detail::NoIndex)
    {
        row_tip_->handle_pointer(e); // just placed over the row: it starts timing with this event
    }

    // An overlaying pane: its light-dismiss layer covers the content, which gets no input
    // (hit_test_children()), and a release on it closes the pane.
    if (light_dismissible())
    {
        if (e.action == PointerAction::Up && e.button == PointerButton::Left && !pane.contains(e.position) &&
            !over_button(e.position) && content_rect_.contains(e.position))
        {
            request_pane_open(false, true);
        }
        return e.action == PointerAction::Down || e.action == PointerAction::Up;
    }
    // A pane click never also reaches the content underneath.
    return e.action == PointerAction::Down && (on_items || in_pane || top_bar_rect_.contains(e.position));
}

void NavigationShellControl::hit_test_children(Point p, std::vector<Element*>& out) const
{
    // The buttons sit over everything.
    for (ToolTipControl* tip : {back_tip_.get(), close_tip_.get(), toggle_tip_.get()})
    {
        if (tip->bounds().width > 0.0f)
        {
            out.push_back(tip);
        }
    }
    if (search_button_->bounds().width > 0.0f)
    {
        out.push_back(search_button_);
    }
    if (row_tip_item_ != detail::NoIndex)
    {
        out.push_back(row_tip_.get());
    }
    // The pane's (or top bar's) own elements.
    for (Element* el : {auto_suggest_box_.get(), pane_header_.get(), pane_custom_content_.get(), pane_footer_.get()})
    {
        if (el != nullptr)
        {
            out.push_back(el);
        }
    }
    // The content, where nothing of the pane, the top bar or a light-dismiss layer covers it.
    const bool in_pane = !top_mode() && pane_live() && pane_bounds().contains(p);
    const Hit  hit     = hit_test(p);
    if (light_dismissible() || in_pane || top_bar_rect_.contains(p) || hit.item != detail::NoIndex || hit.overflow_button)
    {
        return;
    }
    if (Element* content = active_content(); content != nullptr)
    {
        out.push_back(content);
    }
}

bool NavigationShellControl::covers_children_at(Point p) const
{
    // The overlay scrollbars over the pane's own elements, as in Scroll.
    if (top_mode() || !pane_live() || !pane_bounds().contains(p))
    {
        return false;
    }
    return (menu_content_height_ > menu_viewport_.height && scrollbar_.band_bounds(menu_bar_rect_).contains(p)) ||
           (footer_content_height_ > footer_viewport_.height &&
            footer_scrollbar_.band_bounds(footer_bar_rect_).contains(p));
}

bool NavigationShellControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    if (!top_mode() && pane_live() && pane_bounds().contains(e.position) && !e.horizontal_wheel)
    {
        if (menu_bar_rect_.contains(e.position) && menu_content_height_ > menu_viewport_.height)
        {
            set_pane_scroll_offset(pane_scroll_offset_ + e.wheel_delta);
            // A wheel turn counts as scrollbar activity even when the cursor never crosses the band.
            scrollbar_.notify_activity();
            return true;
        }
        if (footer_bar_rect_.contains(e.position) && footer_content_height_ > footer_viewport_.height)
        {
            footer_scroll_offset_ += e.wheel_delta;
            clamp_scroll();
            footer_scrollbar_.notify_activity();
            mark_dirty(DirtyFlags::Visual);
            return true;
        }
        return true; // a pane wheel never also reaches the content beside it
    }
    // The content had the turn first (handle_wheel()); an overlaying pane takes what is left.
    return light_dismissible();
}

// ---- focus ----

void NavigationShellControl::collect_focusables(std::vector<Element*>& out)
{
    const auto push = [&out](Element* el)
    {
        if (el != nullptr && el->bounds().width > 0.0f)
        {
            el->collect_focusables(out);
        }
    };
    const auto push_items = [this, &out]
    {
        if (focusable())
        {
            out.push_back(this);
        }
    };
    if (trapping_)
    {
        // The pane alone, while it overlays: Tab cycles inside it.
        push(pane_header_.get());
        push(search_button_);
        push(auto_suggest_box_.get());
        push(pane_custom_content_.get());
        push_items();
        push(pane_footer_.get());
        return;
    }
    push(back_tip_.get());
    push(close_tip_.get());
    push(toggle_tip_.get());
    push(pane_header_.get());
    if (top_mode())
    {
        push_items();
        push(pane_custom_content_.get());
        push(auto_suggest_box_.get());
        push(pane_footer_.get());
    }
    else if (pane_live())
    {
        push(search_button_);
        push(auto_suggest_box_.get());
        push(pane_custom_content_.get());
        push_items();
        push(pane_footer_.get());
    }
    if (Element* content = active_content(); content != nullptr)
    {
        content->collect_focusables(out);
    }
}

// ---- tree/invalidation plumbing ----

void NavigationShellControl::clear_dirty_override(DirtyFlags flags)
{
    // A child that kept its dirty bits would never propagate another change up to here.
    for (NavEntry* e : all_)
    {
        if (e->info_badge)
        {
            clear_dirty_of(*e->info_badge, flags);
        }
        if (e->animated_icon)
        {
            clear_dirty_of(*e->animated_icon, flags);
        }
    }
    for (Element* el :
         {static_cast<Element*>(back_tip_.get()),
          static_cast<Element*>(close_tip_.get()),
          static_cast<Element*>(toggle_tip_.get()),
          static_cast<Element*>(row_tip_.get()),
          static_cast<Element*>(settings_icon_.get()),
          static_cast<Element*>(search_button_),
          static_cast<Element*>(flyout_list_.get()),
          auto_suggest_box_.get(),
          pane_header_.get(),
          pane_custom_content_.get(),
          pane_footer_.get(),
          active_content()})
    {
        if (el != nullptr)
        {
            clear_dirty_of(*el, flags);
        }
    }
}

void NavigationShellControl::build_visual_tree_override(VisualTree& tree) const
{
    if (Element* content = active_content(); content != nullptr)
    {
        content->build_visual_tree(tree, visual_id());
    }
}

void NavigationShellControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    for (Element* el :
         {static_cast<Element*>(back_tip_.get()),
          static_cast<Element*>(close_tip_.get()),
          static_cast<Element*>(toggle_tip_.get()),
          static_cast<Element*>(search_button_),
          auto_suggest_box_.get(),
          pane_header_.get(),
          pane_custom_content_.get(),
          pane_footer_.get(),
          active_content()})
    {
        if (el != nullptr)
        {
            detail::TreeAccess::sync_composition(*el, compositor, theme);
        }
    }
}

void NavigationShellControl::sync_native(NativeHost& host)
{
    for (Element* el :
         {auto_suggest_box_.get(), pane_header_.get(), pane_custom_content_.get(), pane_footer_.get(), active_content()})
    {
        if (el != nullptr)
        {
            el->sync_native(host);
        }
    }
}

void NavigationShellControl::tick_override(Duration delta)
{
    ticking_ = true;

    if (indicator_from_)
    {
        indicator_ms_ += animation_delta(delta).count();
        if (indicator_ms_ >= IndicatorTotalMs)
        {
            indicator_from_ = std::nullopt;
            indicator_ms_   = 0.0;
        }
        mark_dirty(DirtyFlags::Visual);
    }

    const double target = pane_open_ ? 1.0 : 0.0;
    if (pane_clock_ != target)
    {
        const double duration = overlay_transition_
                                    ? (pane_open_ ? NavigationShellOverlayOpenMs : NavigationShellOverlayCloseMs)
                                    : (pane_open_ ? NavigationShellInlineOpenMs : NavigationShellInlineCloseMs);
        const double step     = animation_delta(delta).count() / duration;
        pane_clock_           = pane_open_ ? std::min(1.0, pane_clock_ + step) : std::max(0.0, pane_clock_ - step);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    if (menu_content_height_ > menu_viewport_.height && scrollbar_.update(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (footer_content_height_ > footer_viewport_.height && footer_scrollbar_.update(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    // A shown bar waiting to fade wakes the window once for it.
    if (const std::optional<Duration> wait = scrollbar_.fade_wait(); wait && menu_content_height_ > menu_viewport_.height)
    {
        request_tick_in(*wait);
    }
    if (const std::optional<Duration> wait = footer_scrollbar_.fade_wait();
        wait && footer_content_height_ > footer_viewport_.height)
    {
        request_tick_in(*wait);
    }

    for (Element* el :
         {static_cast<Element*>(back_tip_.get()),
          static_cast<Element*>(close_tip_.get()),
          static_cast<Element*>(toggle_tip_.get()),
          static_cast<Element*>(row_tip_.get()),
          static_cast<Element*>(search_button_),
          auto_suggest_box_.get(),
          pane_header_.get(),
          pane_custom_content_.get(),
          pane_footer_.get(),
          active_content()})
    {
        if (el != nullptr)
        {
            el->tick(delta);
        }
    }
    for (NavEntry* e : all_)
    {
        if (e->info_badge)
        {
            e->info_badge->tick(delta);
        }
    }

    // Each row's AnimatedIcon state follows the row's pointer state.
    // Synced here because every hover/press change comes through a tick.
    const auto state_of = [this](std::size_t item) -> const char*
    {
        switch (row_state(is_enabled(), item == hovered_item_, item == pressed_item_))
        {
            case ButtonState::Pressed:
                return "Pressed";
            case ButtonState::Hover:
                return "PointerOver";
            default:
                return "Normal";
        }
    };
    for (std::size_t i = 0; i < all_.size(); ++i)
    {
        if (AnimatedIconControl* icon = all_[i]->animated_icon.get(); icon != nullptr)
        {
            icon->set_state(state_of(i));
            icon->tick(delta);
        }
    }
    settings_icon_->set_state(state_of(Settings));
    settings_icon_->tick(delta);
}

void NavigationShellControl::collect_live(std::vector<Element*>& out)
{
    // Itself: the flyout, and Window polls take_focus_request() on every owner.
    out.push_back(this);
    for (Element* el : {static_cast<Element*>(back_tip_.get()),
                        static_cast<Element*>(close_tip_.get()),
                        static_cast<Element*>(toggle_tip_.get()),
                        static_cast<Element*>(row_tip_.get())})
    {
        out.push_back(el);
    }
    for (Element* el :
         {auto_suggest_box_.get(), pane_header_.get(), pane_custom_content_.get(), pane_footer_.get(), active_content()})
    {
        if (el != nullptr)
        {
            detail::TreeAccess::collect_live(*el, out);
        }
    }
}

void NavigationShellControl::automation_children(std::vector<Element*>& out) const
{
    for (Element* el : {static_cast<Element*>(back_tip_.get()), static_cast<Element*>(toggle_tip_.get())})
    {
        if (el->bounds().width > 0.0f)
        {
            out.push_back(el);
        }
    }
    for (Element* el :
         {auto_suggest_box_.get(), pane_header_.get(), pane_custom_content_.get(), pane_footer_.get(), active_content()})
    {
        if (el != nullptr)
        {
            out.push_back(el);
        }
    }
}


// ---- render ----

namespace
{
constexpr IconGlyph MoreGlyph{0xE712, "Segoe Fluent Icons", 16.0f};

// The left-pane rows use ListView's subtle fill colors. ThemeRowSelection::IndicatorOnly, because
// the accent bar is the whole selected cue.
std::optional<Color> row_background(const Theme& theme, ButtonState state, bool selected)
{
    if (detail::controls(theme).list_item_subtle_selection)
    {
        const Color bg = detail::subtle_row_background(theme, state, selected, ThemeRowSelection::IndicatorOnly);
        return bg.a > 0.0f ? std::optional<Color>{bg} : std::nullopt;
    }
    if (selected)
    {
        // Under MDL2 a selected row takes a list's low accent highlight.
        return detail::controls(theme).row_selection_fills
                   ? detail::controls(theme).row_selection_fills->rest
                   : theme.colors().selection;
    }
    if (state == ButtonState::Hover || state == ButtonState::Pressed)
    {
        return theme.colors().control_fill_hover.with_alpha(NavigationShellHoverOverlayAlpha);
    }
    return std::nullopt;
}

Color header_text_color(const Theme& theme, bool enabled)
{
    if (!enabled)
    {
        return theme.colors().text_disabled;
    }
    // A section header is the secondary text color.
    return detail::controls(theme).list_item_subtle_selection
               ? theme.palette.text_fill_secondary
               : detail::controls(theme).nav_pane.text.value_or(theme.colors().text);
}

Color indicator_fill(const Theme& theme, bool enabled)
{
    return enabled ? theme.colors().accent : theme.accent_button_disabled.background;
}

// The accent bar's stretch between two positions along one axis (IndicatorLead/IndicatorTrail):
// the low and high edges for `from_lo` -> `to_lo` at `ms` into the clock.
std::pair<float, float> stretch(float from_lo, float to_lo, float length, double ms)
{
    const bool  forward = to_lo > from_lo;
    const float lead_t  = easing::evaluate(IndicatorLead, ms / IndicatorLeadMs);
    const float trail_t = easing::evaluate(IndicatorTrail, (ms - IndicatorLeadMs) / (IndicatorTotalMs - IndicatorLeadMs));
    const float lo = from_lo + (to_lo - from_lo) * (forward ? trail_t : lead_t);
    const float hi = from_lo + length + (to_lo - from_lo) * (forward ? lead_t : trail_t);
    return {lo, hi};
}

// A top-bar item's pill: its slot less the item margin, or for an icon-only item a 36px square
// with a 2px margin (Settings is always that).
Rect top_pill(const Rect& slot, bool icon_only)
{
    return icon_only
               ? Rect{slot.x + 2.0f, slot.y + (slot.height - TopIconOnlySize) * 0.5f, TopIconOnlySize, TopIconOnlySize}
               : inset_item_row(slot);
}
} // namespace

Color NavigationShellControl::row_text_color(const Theme& theme, std::size_t index, bool hovered, bool pressed) const
{
    // A row's text is the secondary color only while pressed. A non-Fluent selected row keeps
    // its highlight fill, so its label takes the matching highlight text.
    if (!is_enabled())
    {
        return theme.colors().text_disabled;
    }
    Color color = detail::controls(theme).nav_pane.text.value_or(theme.colors().text);
    if (detail::controls(theme).list_item_subtle_selection)
    {
        if (pressed && hovered)
        {
            color = theme.palette.text_fill_secondary;
        }
    }
    else if (index != detail::NoIndex && index == selected_index_)
    {
        color = theme.colors().selection_text;
    }
    else if (detail::controls(theme).nav_pane.text_hot && (hovered || pressed))
    {
        color = *detail::controls(theme).nav_pane.text_hot;
    }
    return color;
}

void NavigationShellControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    render_content_area(renderer, theme);
    if (top_mode())
    {
        render_top_bar(renderer, theme);
    }
    else
    {
        render_left_pane(renderer, theme);
    }
    // The buttons over everything.
    for (const ToolTipControl* tip : {back_tip_.get(), close_tip_.get(), toggle_tip_.get()})
    {
        if (tip->bounds().width > 0.0f)
        {
            tip->render(renderer, context);
        }
    }
}

void NavigationShellControl::render_content_area(Renderer& renderer, const Theme& theme) const
{
    // The content area: its background inside a border edge.
    if (theme.surfaces.nav_content && content_rect_.width > 0.0f && content_rect_.height > 0.0f)
    {
        const bool  side   = !top_mode() && display_mode_ != NavigationShellDisplayMode::Minimal && pane_visible_;
        const float radius = side ? ContentCornerRadius : 0.0f;
        const Rect  extended{content_rect_.x,
                             content_rect_.y,
                             content_rect_.width + radius + 1.0f,
                             content_rect_.height + radius + 1.0f};
        renderer.push_clip(content_rect_);
        renderer.fill_rounded_rect(extended, radius, *theme.surfaces.nav_content);
        if (theme.surfaces.nav_content_stroke)
        {
            if (side)
            {
                stroke_rounded_rect_inside(renderer, extended, radius, *theme.surfaces.nav_content_stroke, 1.0f);
            }
            else
            {
                renderer.fill_rounded_rect(Rect{content_rect_.x, content_rect_.y, content_rect_.width, 1.0f},
                                           0.0f,
                                           *theme.surfaces.nav_content_stroke);
            }
        }
        renderer.pop_clip();
    }
    if (show_header() && header_rect_.width > 0.0f)
    {
        TextStyle style  = theme.type.title;
        style.horizontal = TextAlign::Leading;
        style.vertical   = TextAlign::Leading;
        renderer.push_clip(header_rect_);
        renderer.draw_text(header_, header_rect_, theme.colors().text, style);
        renderer.pop_clip();
    }
    if (Element* content = active_content(); content != nullptr)
    {
        content->render(renderer, theme);
    }
}

void NavigationShellControl::render_left_rows(
    Renderer&               renderer,
    const Theme&            theme,
    const std::vector<Row>& rows,
    const Rect&             viewport,
    float                   scroll,
    bool                    compact) const
{
    if (viewport.width <= 0.0f || viewport.height <= 0.0f)
    {
        return;
    }
    renderer.push_clip(viewport);
    float y = viewport.y - scroll;
    for (const Row& row : rows)
    {
        const float h = row_height(row, compact);
        const Rect  slot{viewport.x, y, viewport.width, h};
        y += h;
        if (h <= 0.0f || slot.bottom() <= viewport.y || slot.y >= viewport.bottom())
        {
            continue;
        }
        const bool      settings = row.item == Settings;
        const NavEntry* e        = settings ? nullptr : all_[row.item];
        if (e != nullptr && e->kind == NavEntryKind::Header)
        {
            renderer.draw_text(e->text,
                               Rect{slot.x + HeaderInsetLeft,
                                    slot.y,
                                    std::max(0.0f, slot.width - 2.0f * HeaderInsetLeft),
                                    slot.height},
                               header_text_color(theme, is_enabled()),
                               header_item_style(theme));
            continue;
        }
        if (e != nullptr && e->kind == NavEntryKind::Separator)
        {
            draw_divider(renderer, theme, Rect{slot.x, slot.y + 3.0f, slot.width, 1.0f}, false);
            continue;
        }

        const Rect pill     = inset_item_row(slot);
        const bool hovered  = row.item == hovered_item_;
        const bool pressed  = row.item == pressed_item_;
        const bool selected = row.item == selected_index_;
        const auto state    = row_state(is_enabled(), hovered, pressed);
        if (const auto bg = row_background(theme, state, selected))
        {
            // A row's pill takes the control corner radius.
            renderer.fill_rounded_rect(pill, theme.corner_radius, *bg);
        }
        const Color text_color = row_text_color(theme, row.item, hovered, pressed);

        // A top-level row while closed compact: no indent, no chevron, the label right after the icon
        // column (clipped by the strip), and the badge in the pill's top-right corner.
        const bool  compact_row = compact && row.depth == 0;
        const float indent      = compact_row ? 0.0f : static_cast<float>(row.depth) * IndentPerDepth;
        const bool  group       = e != nullptr && !e->children.empty();
        const bool  has_icon    = settings || (e != nullptr && (!e->icon.empty() || e->animated_icon));
        const float icon_col    = has_icon ? IconColumnWidth : NoIconColumn;
        if (has_icon)
        {
            const Rect icon_rect{pill.x + indent + (IconColumnWidth - NavigationShellIconSize) * 0.5f,
                                 pill.y + (pill.height - NavigationShellIconSize) * 0.5f,
                                 NavigationShellIconSize,
                                 NavigationShellIconSize};
            draw_item_icon(renderer, theme, settings ? nullptr : e, icon_rect, text_color);
        }
        float trail       = compact_row ? 0.0f : ItemTextTrailInset;
        float badge_right = pill.right() - BadgeTrailInset;
        if (group && !compact_row)
        {
            trail += NavigationShellChevronColumnWidth;
            badge_right -= NavigationShellChevronColumnWidth;
            draw_disclosure(renderer,
                            theme,
                            e->expanded ? glyphs::NavChevronExpanded : glyphs::NavChevronCollapsed,
                            e->expanded ? ArrowDirection::Up : ArrowDirection::Down,
                            Rect{pill.right() - ChevronBoxWidth, pill.y, ChevronBoxWidth, pill.height},
                            text_color,
                            NavigationShellChevronGlyphSize);
        }
        if (e != nullptr && e->info_badge && row.item < badge_sizes_.size())
        {
            const Size badge = badge_sizes_[row.item];
            // Arranged here rather than in arrange_override(): a row's position is a render-time
            // fact in this control (the pane scrolls without a re-arrange).
            const Rect at = compact_row
                                ? Rect{pill.right() - CompactBadgeInset - badge.width,
                                       pill.y + CompactBadgeInset,
                                       badge.width,
                                       badge.height}
                                : Rect{badge_right - badge.width,
                                       pill.y + (pill.height - badge.height) * 0.5f,
                                       badge.width,
                                       badge.height};
            e->info_badge->arrange(at);
            if (!compact_row)
            {
                trail += badge.width;
            }
        }
        const float        text_left = pill.x + indent + icon_col + (compact_row ? 0.0f : LabelLead);
        const Rect         text_rect{text_left, pill.y, std::max(0.0f, pill.right() - trail - text_left), pill.height};
        const std::string& text = e != nullptr ? e->text : text_at(Settings);
        if (compact_row)
        {
            renderer.push_clip(pill);
        }
        renderer.draw_text(text, text_rect, text_color, item_label_style(theme));
        if (compact_row)
        {
            renderer.pop_clip();
        }
        if (e != nullptr && e->info_badge)
        {
            e->info_badge->render(renderer, theme);
        }
    }
    renderer.pop_clip();
}

void NavigationShellControl::draw_item_icon(
    Renderer&       renderer,
    const Theme&    theme,
    const NavEntry* e,
    const Rect&     rect,
    const Color&    color) const
{
    // Arranged where it is drawn, as the InfoBadges are: the pane scrolls without a re-arrange.
    AnimatedIconControl* animated = e == nullptr ? settings_icon_.get() : e->animated_icon.get();
    if (animated != nullptr)
    {
        animated->arrange(rect);
        animated->render_in(renderer, theme, color);
        return;
    }
    if (e != nullptr && e->icon)
    {
        e->icon.draw_in_slot(renderer, rect, color, NavigationShellIconSize, theme.icon_font_family);
    }
}

void NavigationShellControl::draw_left_indicator(Renderer& renderer, const Theme& theme) const
{
    if (selected_index_ == detail::NoIndex || !detail::controls(theme).accent_selection_indicator)
    {
        return;
    }
    // The bar climbs to the nearest shown
    // ancestor. The pill does not follow, that ancestor is not selected.
    std::optional<Rect> to = item_bounds(selected_index_);
    for (std::size_t p = parent_of(selected_index_); !to && p != detail::NoIndex; p = parent_of(p))
    {
        to = item_bounds(p);
    }
    if (!to)
    {
        return;
    }
    const float height = detail::controls(theme).nav_indicator.size.height;
    float       top    = to->y + (to->height - height) * 0.5f;
    float       bottom = top + height;
    if (indicator_from_ && std::abs(indicator_from_->x - to->x) < 0.5f)
    {
        const float from_top  = indicator_from_->y + (indicator_from_->height - height) * 0.5f;
        std::tie(top, bottom) = stretch(from_top, top, height, indicator_ms_);
    }
    renderer.fill_rounded_rect(Rect{to->x + detail::controls(theme).nav_indicator.left_inset,
                                    top,
                                    detail::controls(theme).nav_indicator.size.width,
                                    std::max(0.0f, bottom - top)},
                               detail::controls(theme).nav_indicator.radius,
                               indicator_fill(theme, is_enabled()));
}

void NavigationShellControl::render_left_pane(Renderer& renderer, const Theme& theme) const
{
    if (!pane_live())
    {
        return;
    }
    const Rect visible = pane_bounds();
    if (visible.width <= 0.0f)
    {
        return;
    }

    // An overlaying pane: in-app acrylic (its fallback color), a 1px frame, the overlay corner
    // radius and a 32 DIP elevation shadow. Otherwise transparent unless the era sets `nav_pane`.
    if (pane_overlaying_)
    {
        const float radius = theme.overlay_corner_radius;
        const Rect  shape{visible.x - radius, visible.y, visible.width + radius, visible.height};
        if (theme.popup_shadow == ThemePopupShadow::AeroDropShadow)
        {
            draw_aero_popup_shadow(renderer, visible, radius);
        }
        else if (theme.popup_shadow != ThemePopupShadow::None)
        {
            draw_overlay_shadow(renderer, visible, radius, theme.dark);
        }
        renderer.push_clip(visible);
        if (detail::controls(theme).nav_pane.background)
        {
            renderer.fill_rounded_rect_gradient(shape, radius, *detail::controls(theme).nav_pane.background);
        }
        else
        {
            renderer.fill_rounded_rect(shape, radius, theme.surfaces.menu);
        }
        stroke_rounded_rect_inside(renderer, shape, radius, theme.colors().divider, 1.0f);
        renderer.pop_clip();
    }
    else if (detail::controls(theme).nav_pane.background)
    {
        renderer.fill_rounded_rect_gradient(visible, 0.0f, *detail::controls(theme).nav_pane.background);
    }

    renderer.push_clip(visible);
    const bool compact = closed_compact();
    render_left_rows(renderer, theme, menu_rows_, menu_viewport_, pane_scroll_offset_, compact);
    if (separator_visible_)
    {
        draw_divider(renderer, theme, Rect{separator_rect_.x, separator_rect_.y + 3.0f, separator_rect_.width, 1.0f}, false);
    }
    render_left_rows(renderer, theme, footer_rows_, footer_viewport_, footer_scroll_offset_, compact);
    for (const Element* el : {pane_header_.get(), auto_suggest_box_.get(), pane_custom_content_.get(), pane_footer_.get()})
    {
        if (el != nullptr && el->bounds().width > 0.0f)
        {
            el->render(renderer, theme);
        }
    }
    if (search_button_->bounds().width > 0.0f)
    {
        search_button_->render(renderer, theme);
    }
    draw_left_indicator(renderer, theme);

    renderer.pop_clip();

    // Painted on top of the clipped rows, the way an overlay scrollbar sits above content.
    if (menu_content_height_ > menu_viewport_.height)
    {
        scrollbar_.render(renderer, theme, menu_bar_rect_, menu_content_height_, pane_scroll_offset_);
    }
    if (footer_content_height_ > footer_viewport_.height)
    {
        footer_scrollbar_.render(renderer, theme, footer_bar_rect_, footer_content_height_, footer_scroll_offset_);
    }
}

void NavigationShellControl::render_top_slot(Renderer& renderer, const Theme& theme, const TopSlot& slot) const
{
    if (slot.item == detail::NoIndex)
    {
        // The overflow button: a square 40x40 taking a top-bar item's hover and pressed washes,
        // and the 0xE712 glyph at 16.
        const auto state = row_state(is_enabled(), overflow_hovered_, overflow_pressed_);
        if (state == ButtonState::Hover || state == ButtonState::Pressed)
        {
            if (const auto bg = row_background(theme, state, false))
            {
                renderer.fill_rounded_rect(slot.rect, 0.0f, *bg);
            }
        }
        const Color color = row_text_color(theme, detail::NoIndex, overflow_hovered_, overflow_pressed_);
        draw_icon(renderer, MoreGlyph, slot.rect, color, MoreGlyph.size, theme.icon_font_family);
        return;
    }
    const bool      settings = slot.item == Settings;
    const NavEntry* e        = settings ? nullptr : all_[slot.item];
    if (e != nullptr && e->kind == NavEntryKind::Separator)
    {
        // A top-bar separator: a vertical line 24 tall, a 3,0,4,0 margin, centered.
        draw_divider(renderer,
                     theme,
                     Rect{slot.rect.x + 3.0f, slot.rect.y + (slot.rect.height - 24.0f) * 0.5f, 1.0f, 24.0f},
                     true);
        return;
    }
    if (e != nullptr && e->kind == NavEntryKind::Header)
    {
        renderer.draw_text(e->text,
                           Rect{slot.rect.x + HeaderInsetTop,
                                slot.rect.y,
                                std::max(0.0f, slot.rect.width - 2.0f * HeaderInsetTop),
                                slot.rect.height},
                           header_text_color(theme, is_enabled()),
                           header_item_style(theme));
        return;
    }

    const bool has_label = e != nullptr && !e->text.empty();
    const bool has_icon  = e == nullptr || !e->icon.empty() || e->animated_icon;
    const bool icon_only = !has_label;
    const Rect pill      = top_pill(slot.rect, icon_only);
    const bool hovered   = slot.item == hovered_item_;
    const bool pressed   = slot.item == pressed_item_;
    const auto state     = row_state(is_enabled(), hovered, pressed);
    // A selected top-bar item has no background: the underline is the whole cue. A
    // fill-era theme keeps its highlight instead.
    const bool selected = slot.item == selected_index_;
    if (detail::controls(theme).list_item_subtle_selection)
    {
        std::size_t top_of_selection = selected_index_;
        while (top_of_selection < all_.size() && parent_of(top_of_selection) != detail::NoIndex)
        {
            top_of_selection = parent_of(top_of_selection);
        }
        if (top_of_selection != slot.item && (state == ButtonState::Hover || state == ButtonState::Pressed))
        {
            renderer.fill_rounded_rect(pill,
                                       theme.corner_radius,
                                       detail::subtle_row_background(theme, state, false, ThemeRowSelection::IndicatorOnly));
        }
    }
    else if (const auto bg = row_background(theme, state, selected))
    {
        renderer.fill_rounded_rect(pill, theme.corner_radius, *bg);
    }
    const Color color = row_text_color(theme, slot.item, hovered, pressed);
    if (icon_only)
    {
        draw_item_icon(renderer,
                       theme,
                       settings ? nullptr : e,
                       Rect{pill.x + (pill.width - NavigationShellIconSize) * 0.5f,
                            pill.y + (pill.height - NavigationShellIconSize) * 0.5f,
                            NavigationShellIconSize,
                            NavigationShellIconSize},
                       color);
        if (e != nullptr && !e->children.empty())
        {
            const bool open = flyout_kind_ == FlyoutKind::Children && flyout_group_ == slot.item;
            draw_disclosure(renderer,
                            theme,
                            open ? glyphs::NavChevronExpanded : glyphs::NavChevronCollapsed,
                            open ? ArrowDirection::Up : ArrowDirection::Down,
                            Rect{pill.right(), pill.y, ChevronBoxWidth, pill.height},
                            color,
                            NavigationShellChevronGlyphSize);
        }
        return;
    }
    float x = pill.x + TopIconLead;
    if (has_icon)
    {
        draw_item_icon(renderer,
                       theme,
                       settings ? nullptr : e,
                       Rect{x, pill.y + (pill.height - NavigationShellIconSize) * 0.5f, NavigationShellIconSize, NavigationShellIconSize},
                       color);
        x += NavigationShellIconSize + TopIconGap;
    }
    const bool  group       = !e->children.empty();
    const float chevron_col = group ? ChevronBoxWidth - (has_icon ? 16.0f : 12.0f) : 0.0f;
    const Size  badge       = e->info_badge && slot.item < badge_sizes_.size() ? badge_sizes_[slot.item] : Size{};
    const float badge_extra = e->info_badge ? std::max(0.0f, badge.width - 16.0f + 2.0f) : 0.0f;
    const float text_w      = std::max(0.0f, pill.right() - TopLabelTrail - chevron_col - badge_extra - x);
    renderer.draw_text(e->text, Rect{x, pill.y, text_w, pill.height}, color, item_label_style(theme));
    float column_end = x + text_w + TopLabelTrail;
    if (group)
    {
        const float box_x = column_end - (has_icon ? 16.0f : 12.0f);
        const bool  open  = flyout_kind_ == FlyoutKind::Children && flyout_group_ == slot.item;
        draw_disclosure(renderer,
                        theme,
                        open ? glyphs::NavChevronExpanded : glyphs::NavChevronCollapsed,
                        open ? ArrowDirection::Up : ArrowDirection::Down,
                        Rect{box_x, pill.y, ChevronBoxWidth, pill.height},
                        color,
                        NavigationShellChevronGlyphSize);
        column_end = box_x + ChevronBoxWidth;
    }
    if (e->info_badge)
    {
        // The badge: a -16,0,2,13 margin, centered, up and over the item's corner.
        const Rect at{column_end - 16.0f, pill.y + (pill.height - 13.0f - badge.height) * 0.5f, badge.width, badge.height};
        e->info_badge->arrange(at);
        e->info_badge->render(renderer, theme);
    }
}

void NavigationShellControl::draw_top_indicator(Renderer& renderer, const Theme& theme) const
{
    if (selected_index_ == detail::NoIndex || !detail::controls(theme).accent_selection_indicator)
    {
        return;
    }
    std::size_t shown = selected_index_;
    while (shown < all_.size() && parent_of(shown) != detail::NoIndex)
    {
        shown = parent_of(shown);
    }
    const TopSlot* target = nullptr;
    for (const TopSlot& slot : top_slots_)
    {
        if (slot.item == shown)
        {
            target = &slot;
        }
    }
    if (target == nullptr)
    {
        return; // behind the overflow button: no indicator
    }
    const auto bar = [this](std::size_t item, const Rect& slot)
    {
        const bool  icon_only = item == Settings || (item < all_.size() && all_[item]->text.empty());
        const Rect  pill      = top_pill(slot, icon_only);
        const float bottom    = pill.bottom() - (icon_only ? 0.0f : TopIndicatorBottom);
        return Rect{pill.x + (pill.width - TopIndicatorWidth) * 0.5f,
                    bottom - TopIndicatorHeight,
                    TopIndicatorWidth,
                    TopIndicatorHeight};
    };
    Rect to = bar(target->item, target->rect);
    if (indicator_from_ && std::abs(indicator_from_->height - TopBarHeight) < 0.5f)
    {
        const Rect from          = bar(detail::NoIndex, *indicator_from_);
        const auto [left, right] = stretch(from.x, to.x, TopIndicatorWidth, indicator_ms_);
        to.x                     = left;
        to.width                 = std::max(0.0f, right - left);
    }
    renderer.fill_rounded_rect(to, detail::controls(theme).nav_indicator.radius, indicator_fill(theme, is_enabled()));
}

void NavigationShellControl::render_top_bar(Renderer& renderer, const Theme& theme) const
{
    renderer.push_clip(top_bar_rect_);
    // The top bar is transparent over the page, unless the era paints a pane.
    if (detail::controls(theme).nav_pane.background)
    {
        renderer.fill_rounded_rect_gradient(top_bar_rect_, 0.0f, *detail::controls(theme).nav_pane.background);
    }
    for (const TopSlot& slot : top_slots_)
    {
        render_top_slot(renderer, theme, slot);
    }
    if (title_rect_.width > 0.0f)
    {
        renderer.draw_text(pane_title_, title_rect_, theme.colors().text, header_item_style(theme));
    }
    for (const Element* el : {pane_header_.get(), pane_custom_content_.get(), auto_suggest_box_.get(), pane_footer_.get()})
    {
        if (el != nullptr && el->bounds().width > 0.0f)
        {
            el->render(renderer, theme);
        }
    }
    draw_top_indicator(renderer, theme);
    renderer.pop_clip();
}

Rect NavigationShellControl::focus_ring_rect(const Theme& /*theme*/) const
{
    if (flyout_open())
    {
        return Rect{}; // the flyout rings its own row
    }
    if (!top_mode())
    {
        // Around the cursor's own pill.
        return item_bounds(cursor_index_).value_or(Rect{});
    }
    for (const TopSlot& slot : top_slots_)
    {
        if (slot.item == detail::NoIndex ? cursor_index_ == OverflowStop : slot.item == cursor_index_)
        {
            const bool icon_only = slot.item == Settings || (slot.item < all_.size() && all_[slot.item]->text.empty());
            return slot.item == detail::NoIndex ? slot.rect : top_pill(slot.rect, icon_only);
        }
    }
    return Rect{};
}

// ---- the flyout's rows ----

float NavFlyoutList::row_height(const NavigationShellControl::Row& row) const
{
    const NavEntry* e = owner_.entry(row.item);
    if (e != nullptr && e->kind == NavEntryKind::Separator)
    {
        return SeparatorRowHeight;
    }
    return owner_.top_mode() ? OverflowRowHeight : owner_.item_row_height_;
}

float NavFlyoutList::natural_row_width(const NavigationShellControl::Row& row, const MeasureContext& context) const
{
    const NavEntry* e = owner_.entry(row.item);
    if (e == nullptr || e->kind == NavEntryKind::Separator)
    {
        return 0.0f;
    }
    if (e->kind == NavEntryKind::Header)
    {
        return context.measurer.measure(e->text, header_item_style(context.theme)).width + 2.0f * HeaderInsetLeft;
    }
    const float indent = static_cast<float>(row.depth) * IndentPerDepth;
    const float text   = std::ceil(context.measurer.measure(e->text, item_label_style(context.theme)).width);
    const bool  group  = !e->children.empty();
    const float badge = e->info_badge && row.item < owner_.badge_sizes_.size() ? owner_.badge_sizes_[row.item].width : 0.0f;
    if (owner_.top_mode())
    {
        const float lead = e->icon ? OverflowIconLead + NavigationShellIconSize + OverflowLabelGap : OverflowIconLead;
        return indent + lead + text + OverflowLabelTrail + (group ? ChevronBoxWidth - 12.0f : 0.0f) + badge;
    }
    const float icon_col = e->icon ? IconColumnWidth : NoIconColumn;
    return 2.0f * ItemMarginH + indent + icon_col + LabelLead + text + ItemTextTrailInset +
           (group ? NavigationShellChevronColumnWidth : 0.0f) + badge;
}

Size NavFlyoutList::measure_override(Size, const MeasureContext& context) const
{
    float width  = 0.0f;
    float height = 2.0f * (1.0f + NavigationShellFlyoutPadding); // the flyout's 1px frame and its 0,2 padding
    for (const NavigationShellControl::Row& row : owner_.flyout_rows_)
    {
        width = std::max(width, natural_row_width(row, context));
        height += row_height(row);
    }
    return Size{std::clamp(width + 2.0f, NavigationShellFlyoutMinWidth, NavigationShellFlyoutMaxWidth), height};
}

std::size_t NavFlyoutList::row_at(Point p) const
{
    if (!bounds().contains(p))
    {
        return detail::NoIndex;
    }
    float y = bounds().y + 1.0f + NavigationShellFlyoutPadding;
    for (std::size_t r = 0; r < owner_.flyout_rows_.size(); ++r)
    {
        const float h = row_height(owner_.flyout_rows_[r]);
        if (p.y >= y && p.y < y + h)
        {
            return owner_.cursor_stop(owner_.flyout_rows_[r].item) ? r : detail::NoIndex;
        }
        y += h;
    }
    return detail::NoIndex;
}

void NavFlyoutList::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    // The flyout surface's own chrome: the in-app acrylic, the popup frame, the overlay corner radius.
    const Color fill   = theme.surfaces.menu;
    const Color stroke = theme.surfaces.menu_border.value_or(theme.surfaces.field_border);
    fill_overlay_surface(renderer, theme, bounds(), theme.overlay_corner_radius, fill);
    stroke_rounded_rect_inside(renderer, bounds(), theme.overlay_corner_radius, stroke, theme.border_width);
    // The flyout's colors, not the page's (FlyoutSurface's substitution).
    const Theme inside = theme.with_content_colors(theme.surfaces.menu_text, fill);

    renderer.push_clip(bounds());
    const bool overflow_style = owner_.top_mode();
    float      y              = bounds().y + 1.0f + NavigationShellFlyoutPadding;
    for (std::size_t r = 0; r < owner_.flyout_rows_.size(); ++r)
    {
        const NavigationShellControl::Row& row = owner_.flyout_rows_[r];
        const NavEntry*                    e   = owner_.entry(row.item);
        const float                        h   = row_height(row);
        const Rect                         slot{bounds().x + 1.0f, y, std::max(0.0f, bounds().width - 2.0f), h};
        y += h;
        if (e == nullptr)
        {
            continue;
        }
        if (e->kind == NavEntryKind::Separator)
        {
            draw_divider(renderer, inside, Rect{slot.x, slot.y + 3.0f, slot.width, 1.0f}, false);
            continue;
        }
        if (e->kind == NavEntryKind::Header)
        {
            renderer.draw_text(e->text,
                               Rect{slot.x + HeaderInsetLeft,
                                    slot.y,
                                    std::max(0.0f, slot.width - 2.0f * HeaderInsetLeft),
                                    slot.height},
                               header_text_color(inside, owner_.is_enabled()),
                               header_item_style(inside));
            continue;
        }
        const bool  is_hovered = r == hovered;
        const bool  is_pressed = r == pressed;
        const bool  selected   = row.item == owner_.selected_index_;
        const auto  state      = row_state(owner_.is_enabled(), is_hovered, is_pressed);
        const Rect  pill       = overflow_style ? slot : inset_item_row(slot);
        const float radius     = overflow_style ? 0.0f : inside.corner_radius;
        if (const auto bg = row_background(inside, state, selected))
        {
            renderer.fill_rounded_rect(pill, radius, *bg);
        }
        Color color = owner_.is_enabled() ? inside.colors().text : inside.colors().text_disabled;
        if (owner_.is_enabled() && detail::controls(inside).list_item_subtle_selection && is_pressed && is_hovered)
        {
            color = inside.palette.text_fill_secondary;
        }
        else if (owner_.is_enabled() && !detail::controls(inside).list_item_subtle_selection && selected)
        {
            color = inside.colors().selection_text;
        }
        const float indent = static_cast<float>(row.depth) * IndentPerDepth;
        const bool  group  = !e->children.empty();
        float       x      = 0.0f;
        float       trail  = 0.0f;
        if (overflow_style)
        {
            if (selected && detail::controls(inside).accent_selection_indicator)
            {
                renderer.fill_rounded_rect(Rect{slot.x + OverflowIndicatorPad,
                                                slot.y + (slot.height - OverflowIndicatorH) * 0.5f,
                                                OverflowIndicatorW,
                                                OverflowIndicatorH},
                                           1.0f,
                                           indicator_fill(inside, owner_.is_enabled()));
            }
            x = slot.x + indent + OverflowIconLead;
            if (e->icon)
            {
                e->icon.draw_in_slot(renderer,
                                     Rect{x,
                                          slot.y + (slot.height - NavigationShellIconSize) * 0.5f,
                                          NavigationShellIconSize,
                                          NavigationShellIconSize},
                                     color,
                                     NavigationShellIconSize,
                                     inside.icon_font_family);
                x += NavigationShellIconSize + OverflowLabelGap;
            }
            trail = OverflowLabelTrail;
            if (group)
            {
                // The chevron's -4,0,-8,0 margin in the overflow flyout: the 40px box ends
                // 6 short of the row.
                const Rect box{slot.right() - 6.0f - ChevronBoxWidth, slot.y, ChevronBoxWidth, slot.height};
                draw_disclosure(renderer,
                                inside,
                                e->expanded ? glyphs::NavChevronExpanded : glyphs::NavChevronCollapsed,
                                e->expanded ? ArrowDirection::Up : ArrowDirection::Down,
                                box,
                                color,
                                NavigationShellChevronGlyphSize);
                trail += ChevronBoxWidth - 12.0f;
            }
        }
        else
        {
            const float icon_col = e->icon ? IconColumnWidth : NoIconColumn;
            if (e->icon)
            {
                e->icon.draw_in_slot(renderer,
                                     Rect{pill.x + indent + (IconColumnWidth - NavigationShellIconSize) * 0.5f,
                                          pill.y + (pill.height - NavigationShellIconSize) * 0.5f,
                                          NavigationShellIconSize,
                                          NavigationShellIconSize},
                                     color,
                                     NavigationShellIconSize,
                                     inside.icon_font_family);
            }
            x     = pill.x + indent + icon_col + LabelLead;
            trail = ItemTextTrailInset;
            if (group)
            {
                draw_disclosure(renderer,
                                inside,
                                e->expanded ? glyphs::NavChevronExpanded : glyphs::NavChevronCollapsed,
                                e->expanded ? ArrowDirection::Up : ArrowDirection::Down,
                                Rect{pill.right() - ChevronBoxWidth, pill.y, ChevronBoxWidth, pill.height},
                                color,
                                NavigationShellChevronGlyphSize);
                trail += NavigationShellChevronColumnWidth;
            }
            if (selected && detail::controls(inside).accent_selection_indicator)
            {
                const float ih = detail::controls(inside).nav_indicator.size.height;
                renderer.fill_rounded_rect(Rect{pill.x + detail::controls(inside).nav_indicator.left_inset,
                                                pill.y + (pill.height - ih) * 0.5f,
                                                detail::controls(inside).nav_indicator.size.width,
                                                ih},
                                           detail::controls(inside).nav_indicator.radius,
                                           indicator_fill(inside, owner_.is_enabled()));
            }
        }
        if (e->info_badge && row.item < owner_.badge_sizes_.size())
        {
            const Size  badge = owner_.badge_sizes_[row.item];
            const float right = pill.right() - BadgeTrailInset - (group ? NavigationShellChevronColumnWidth : 0.0f);
            e->info_badge->arrange(
                Rect{right - badge.width, slot.y + (slot.height - badge.height) * 0.5f, badge.width, badge.height});
            e->info_badge->render(renderer, inside);
            trail += badge.width;
        }
        renderer.draw_text(e->text,
                           Rect{x, slot.y, std::max(0.0f, pill.right() - trail - x), slot.height},
                           color,
                           item_label_style(inside));
        if (owner_.flyout_by_keyboard_ && owner_.is_focused() && row.item == owner_.flyout_cursor_ && focus_visible())
        {
            // Inside the row, as every item in a viewport is ringed.
            const FocusRingStyle ring = inside.focus_ring_style();
            draw_focus_ring(renderer, pill.inset(Thickness{ring.outward_extent()}), radius, ring);
        }
    }
    renderer.pop_clip();
}

bool NavFlyoutList::pointer_override(const PointerEvent& e)
{
    if (!owner_.is_enabled())
    {
        return false;
    }
    const std::size_t hit = row_at(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (hit != hovered)
            {
                hovered = hit;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && hit != detail::NoIndex)
            {
                pressed                    = hit;
                owner_.flyout_by_keyboard_ = false;
                const std::size_t item     = owner_.flyout_rows_[hit].item;
                owner_.flyout_cursor_      = item;
                owner_.invoke(item, false, true);
            }
            break;
        case PointerAction::Up:
            if (pressed != detail::NoIndex)
            {
                pressed = detail::NoIndex;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Leave:
            hovered = detail::NoIndex;
            pressed = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            break;
        default:
            break;
    }
    return false;
}

// NavList::Entry -> ui::NavEntry, depth first. The spec has its own node type because a braced
// list can only build that one, while the control owns the other. A bound text is collected with
// its item's pre-order index, the index the control's positional calls take.
namespace
{
struct BoundNavText
{
    std::size_t               index;
    ui::Binding<std::string>* text;
};

std::vector<ui::NavEntry> to_nav_entries(std::vector<NavList::Entry>& entries,
                                         std::size_t&                 next_index,
                                         std::vector<BoundNavText>&   bound)
{
    std::vector<ui::NavEntry> out;
    out.reserve(entries.size());
    for (NavList::Entry& entry : entries)
    {
        if (entry.text.reactive())
        {
            bound.push_back({next_index, &entry.text});
        }
        ++next_index;
        std::unique_ptr<ui::InfoBadgeControl> badge;
        if (dynamic_cast<ui::InfoBadgeControl*>(entry.info_badge.get()) != nullptr)
        {
            badge.reset(static_cast<ui::InfoBadgeControl*>(entry.info_badge.release()));
        }
        std::unique_ptr<ui::AnimatedIconControl> animated;
        if (entry.animated_icon != nullptr)
        {
            animated = std::make_unique<ui::AnimatedIconControl>();
            animated->set_source(entry.animated_icon);
            if (entry.icon)
            {
                animated->set_fallback(entry.icon);
            }
        }
        out.push_back(
            ui::NavEntry{entry.text.initial(),
                         std::move(entry.node),
                         to_nav_entries(entry.children, next_index, bound),
                         entry.expanded,
                         entry.selects_on_invoke,
                         entry.icon,
                         std::move(badge),
                         entry.kind,
                         std::move(entry.tool_tip),
                         std::move(animated),
                         std::move(entry.key)});
    }
    return out;
}
} // namespace

std::unique_ptr<ui::Element> NavigationShell::build() &&
{
    auto node = std::make_unique<ui::NavigationShellControl>();
    apply(pane_width, *node, [raw = node.get()](const float& v) { raw->set_pane_width(v); });
    apply(item_row_height, *node, [raw = node.get()](const float& v) { raw->set_item_row_height(v); });
    apply(compact_pane_length, *node, [raw = node.get()](const float& v) { raw->set_compact_pane_length(v); });
    apply(compact_mode_threshold_width,
          *node,
          [raw = node.get()](const float& v) { raw->set_compact_mode_threshold_width(v); });
    apply(expanded_mode_threshold_width,
          *node,
          [raw = node.get()](const float& v) { raw->set_expanded_mode_threshold_width(v); });
    if (content)
    {
        node->set_content(content.take());
    }
    if (auto_suggest_box)
    {
        node->set_auto_suggest_box(auto_suggest_box.take());
    }
    if (pane_header_content)
    {
        node->set_pane_header_content(pane_header_content.take());
    }
    if (pane_custom_content)
    {
        node->set_pane_custom_content(pane_custom_content.take());
    }
    if (pane_footer_content)
    {
        node->set_pane_footer_content(pane_footer_content.take());
    }
    std::size_t               next_index = 0;
    std::vector<BoundNavText> bound;
    node->set_items(to_nav_entries(items.entries, next_index, bound));
    node->set_footer_items(to_nav_entries(footer_items.entries, next_index, bound));

    ui::NavigationShellControl* raw = node.get();
    // Before the selection: an item without a key is keyed by its text.
    for (const BoundNavText& item : bound)
    {
        apply(*item.text, *node, [raw, index = item.index](const std::string& value) { raw->set_text(index, value); });
    }
    apply(pane_display_mode,
          *node,
          [raw](const ui::NavigationShellPaneDisplayMode& mode) { raw->set_pane_display_mode(mode); });
    apply(pane_visible, *node, [raw](const bool& value) { raw->set_pane_visible(value); });
    apply(settings_visible, *node, [raw](const bool& value) { raw->set_settings_visible(value); });
    // An empty or unknown key leaves the selection as it is, and a two-way binding then takes the
    // key of the item actually selected.
    const auto selected_writer = ui::shared_writer(selected_key);
    apply(selected_key,
          *node,
          [raw, selected_writer](const std::string& key)
          {
              if (!key.empty())
              {
                  raw->set_selected_key(key);
              }
              if (selected_writer && raw->selected_key() != key)
              {
                  (*selected_writer)(raw->selected_key());
              }
          });
    apply(enabled, *node, [raw](const bool& value) { raw->set_enabled(value); });
    apply(is_pane_open, *node, [raw](const bool& value) { raw->set_pane_open(value); });
    apply(header, *node, [raw](const std::string& value) { raw->set_header(value); });
    apply(always_show_header, *node, [raw](const bool& value) { raw->set_always_show_header(value); });
    apply(pane_title, *node, [raw](const std::string& value) { raw->set_pane_title(value); });
    apply(back_button_visible, *node, [raw](const ui::AutoVisibility& value) { raw->set_back_button_visible(value); });
    apply(back_enabled, *node, [raw](const bool& value) { raw->set_back_enabled(value); });
    apply(pane_toggle_button_visible, *node, [raw](const bool& value) { raw->set_pane_toggle_button_visible(value); });
    apply(selection_follows_focus,
          *node,
          [raw](const ui::NavigationShellSelectionFollowsFocus& value) { raw->set_selection_follows_focus(value); });

    if (auto changed = ui::write_back(selected_writer, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }
    if (on_item_invoked)
    {
        node->on_item_invoked(std::move(on_item_invoked));
    }
    if (on_display_mode_changed)
    {
        node->on_display_mode_changed(std::move(on_display_mode_changed));
    }
    if (on_pane_opening)
    {
        node->on_pane_opening(std::move(on_pane_opening));
    }
    // A two-way `.is_pane_open` follows the control: the user opening or closing it writes the property.
    const auto is_pane_open_writer = ui::shared_writer(is_pane_open);
    if (auto handler = ui::write_back_value<bool>(is_pane_open_writer, true, std::move(on_pane_opened)))
    {
        node->on_pane_opened(std::move(handler));
    }
    if (on_pane_closing)
    {
        node->on_pane_closing(std::move(on_pane_closing));
    }
    if (auto handler = ui::write_back_value<bool>(is_pane_open_writer, false, std::move(on_pane_closed)))
    {
        node->on_pane_closed(std::move(handler));
    }
    if (on_back_requested)
    {
        node->on_back_requested(std::move(on_back_requested));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

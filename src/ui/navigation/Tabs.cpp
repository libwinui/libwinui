// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/Tabs.hpp"

#include "../../render/Compositor.hpp"
#include "../LocalText.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float TabsHoverOverlayAlpha = 0.10f; // non-Fluent fallback wash, see render_tab()

// ---- Tabs metrics ----
// The strip's top padding of 8, a tab's minimum height of 32, and the Equal width mode's
// clamp of 100 to 240.
constexpr float HeaderTopPadding = 8.0f;
constexpr float ItemMinHeight    = 32.0f;
constexpr float ItemMinWidth     = 100.0f;
constexpr float ItemMaxWidth     = 240.0f;
// A tab header's font size is 12: a tab header is not body text.
constexpr float TabsHeaderFontSize = 12.0f;
// A tab's border, its padding (8,3,4,3 with the close button, 8,3,8,3 without), its icon, and
// the close button with its 0xE711 glyph.
constexpr float Border          = 1.0f;
constexpr float PadLeft         = 8.0f;
constexpr float PadRightClose   = 4.0f;
constexpr float PadRightNoClose = 8.0f;
constexpr float PadV            = 3.0f;
constexpr float TabsIconSize    = 16.0f;
constexpr float TabsIconGap     = 10.0f;
constexpr float CloseWidth      = 32.0f;
constexpr float CloseHeight     = 24.0f;
constexpr float CloseGap        = 4.0f;
constexpr float CloseGlyphSize  = 12.0f;
// The tab list has a 4px gap before and after its tabs, and its scrolled content a 1px left
// padding.
constexpr float ListEdge    = 4.0f;
constexpr float ListPadLeft = 1.0f;
// The scroll buttons (32 x 24) with their container paddings, glyphs 0xEDD9/0xEDDA at 8,
// 50 DIPs a click, and held, a repeat after 50 ms and then every 100 ms.
constexpr float  ScrollButtonWidth    = 32.0f;
constexpr float  ScrollButtonHeight   = 24.0f;
constexpr float  ScrollOuterPad       = 8.0f;
constexpr float  ScrollInnerPad       = 3.0f;
constexpr float  ScrollBottomPad      = 3.0f;
constexpr float  ScrollContainer      = ScrollOuterPad + ScrollButtonWidth + ScrollInnerPad;
constexpr float  ScrollAmount         = 50.0f;
constexpr double RepeatDelayMs        = 50.0;
constexpr double TabsRepeatIntervalMs = 100.0;
// The add button: 32 x 24 in a container padded 3,0,0,3, its glyph (0xE710) at 12.
constexpr float AddButtonWidth  = 32.0f;
constexpr float AddButtonHeight = 24.0f;
constexpr float AddPadLeft      = 3.0f;
constexpr float AddPadBottom    = 3.0f;
// The separator: a 1px line at each tab's right edge, inset 8 at the top and bottom.
constexpr float SeparatorInsetV = 8.0f;
// The selected tab's bottom corners curve 4px outward.
constexpr float Flare = 4.0f;
// Dragging starts once the pointer has moved this far from the press (the system drag
// rectangle's half-size, SM_CXDRAG).
constexpr float TabsDragThreshold = 4.0f;

constexpr IconGlyph TabsCloseGlyph{0xE711, "Segoe Fluent Icons", CloseGlyphSize};
constexpr IconGlyph AddGlyph{0xE710, "Segoe Fluent Icons", 12.0f};
constexpr IconGlyph ScrollLeftGlyph{0xEDD9, "Segoe Fluent Icons", 8.0f};
constexpr IconGlyph ScrollRightGlyph{0xEDDA, "Segoe Fluent Icons", 8.0f};

// Tooltip keys for the hovered part of the strip.
constexpr int TipScrollBackward = -2;
constexpr int TipScrollForward  = -3;

class TabsEmptyAnchor final : public Element
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

// A tab's pill rounds only its top two corners: drawn over-tall
// by one radius and clipped.
void fill_top_rounded(Renderer& r, const Rect& rect, float radius, const Color& color)
{
    r.push_clip(rect);
    r.fill_rounded_rect(Rect{rect.left(), rect.top(), rect.width, rect.height + radius}, radius, color);
    r.pop_clip();
}

// The selected tab's outline: top corners at the overlay corner radius, the bottom flaring 4px outward
// into the strip's rule. The arcs are sampled into points for Renderer::fill_polygon().
std::vector<Point> selected_tab_shape(const Rect& slot, float radius)
{
    std::vector<Point> points;
    const auto         arc = [&points](Point center, float r, float from_deg, float to_deg)
    {
        constexpr int Steps = 6;
        for (int i = 0; i <= Steps; ++i)
        {
            const float a = (from_deg + (to_deg - from_deg) * static_cast<float>(i) / Steps) *
                            std::numbers::pi_v<float> / 180.0f;
            points.push_back(Point{center.x + r * std::cos(a), center.y + r * std::sin(a)});
        }
    };
    const float b = slot.bottom();
    const float r = std::min(radius, std::min(slot.width * 0.5f, slot.height - Flare));
    arc(Point{slot.x - Flare, b - Flare}, Flare, 90.0f, 0.0f);         // left flare, up into the side
    arc(Point{slot.x + r, slot.y + r}, r, 180.0f, 270.0f);             // top-left corner
    arc(Point{slot.right() - r, slot.y + r}, r, 270.0f, 360.0f);       // top-right corner
    arc(Point{slot.right() + Flare, b - Flare}, Flare, 180.0f, 90.0f); // right flare, down into the rule
    return points;
}
} // namespace

// ---- automation nodes ----
// A tab is drawn by the strip and has no Element, so each place in the strip gets one of these
// when a client first asks for it. It reads the tab at its index live.

// A closable tab's close button: Invoke asks to close it, as a click does.
class TabsCloseAutomation final : public Element, private InvokeProvider
{
public:
    TabsCloseAutomation(TabsControl* owner, Element* item, std::size_t index) : owner_(owner), index_(index)
    {
        set_parent_of(*this, item);
    }

    void place(const Rect& rect)
    {
        detail::LayoutAccess::set_bounds(*this, rect);
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        (void)out;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return strings().tabs.close_tab;
    }
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
    }
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    bool invoke() override
    {
        if (!owner_->is_enabled() || !owner_->closable(index_))
        {
            return false;
        }
        owner_->request_close(index_);
        return true;
    }

    TabsControl* owner_;
    std::size_t  index_;
};

class TabsItemAutomation final : public Element, private SelectionItemProvider
{
public:
    TabsItemAutomation(TabsControl* owner, std::size_t index) :
        owner_(owner),
        index_(index),
        close_(std::make_unique<TabsCloseAutomation>(owner, this, index))
    {
        set_parent_of(*this, owner);
    }

    void place()
    {
        detail::LayoutAccess::set_bounds(*this, owner_->tab_bounds(index_).value_or(Rect{}));
        close_->place(owner_->close_button_bounds(index_).value_or(Rect{}));
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::TabItem;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        if (owner_->closable(index_))
        {
            out.push_back(close_.get());
        }
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return owner_->header(index_);
    }
    void automation_providers(AutomationProviders& out) override
    {
        out.selection_item = this;
    }
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    [[nodiscard]] bool item_selected() const override
    {
        return owner_->selected_index_ == index_;
    }
    bool select_item() override
    {
        if (!owner_->is_enabled() || index_ >= owner_->tab_count())
        {
            return false;
        }
        owner_->select(index_);
        return true;
    }

    TabsControl*                         owner_;
    std::size_t                          index_;
    std::unique_ptr<TabsCloseAutomation> close_;
};

TabsItemAutomation* TabsControl::item_automation(std::size_t index) const
{
    auto* self = const_cast<TabsControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    while (item_automation_.size() <= index)
    {
        item_automation_.push_back(std::make_unique<TabsItemAutomation>(self, item_automation_.size()));
        item_automation_.back()->place();
    }
    return item_automation_[index].get();
}

void TabsControl::place_item_automation()
{
    for (const auto& item : item_automation_)
    {
        item->place();
    }
}

Element* TabsControl::automation_focus() const
{
    const std::size_t index = cursor_index_ < tabs_.size() ? cursor_index_ : selected_index_;
    return index < tabs_.size() ? item_automation(index) : nullptr;
}

void TabsControl::selected_items(std::vector<Element*>& out) const
{
    if (selected_index_ < tabs_.size())
    {
        out.push_back(item_automation(selected_index_));
    }
}

// ---- construction ----

TabsControl::TabsControl()
{
    auto add = std::make_unique<ButtonControl>();
    add->set_variant(ButtonVariant::Subtle);
    add->set_padding(Thickness{0.0f});
    add->set_content(std::make_unique<IconControl>(AddGlyph));
    add->on_click(
        [this]
        {
            if (on_add_tab_button_click_)
            {
                on_add_tab_button_click_();
            }
        });
    add_tip_    = std::make_unique<ToolTipControl>();
    add_button_ = add_tip_->set_anchor(std::move(add));
    add_tip_->set_content(std::make_unique<TextControl>());
    set_parent_of(*add_tip_, this);
    detail::follow_strings(*this,
                           [this]
                           {
                               add_button_->set_accessibility(Accessibility{.name = strings().tabs.add_tab_name});
                               if (auto* text = dynamic_cast<TextControl*>(add_tip_->content()))
                               {
                                   text->set_text(strings().tabs.add_tab);
                               }
                           });

    hover_tip_ = std::make_unique<ToolTipControl>();
    hover_tip_->set_anchor(std::make_unique<TabsEmptyAnchor>());
    hover_tip_->set_content(std::make_unique<TextControl>());
    set_parent_of(*hover_tip_, this);
}

TabsControl::~TabsControl() = default;

// ---- tabs ----

Element* TabsControl::insert_tab(std::size_t index, ui::Tab spec)
{
    index = std::min(index, tabs_.size());
    Tab tab;
    tab.header        = spec.header.initial();
    tab.key           = spec.key.empty() ? unique_key(tab.header) : std::move(spec.key);
    tab.content       = spec.content.take();
    tab.icon          = spec.icon;
    tab.closable      = spec.closable;
    Element* observer = tab.content.get();
    // A bound header keeps the tab following it. The tab is found by its key, which stays with
    // it when it moves; once it is removed the binding has nothing left to update.
    if (spec.header.reactive())
    {
        add_subscription(spec.header.subscribe(
            [this, key = tab.key](const std::string& value)
            {
                if (const std::optional<std::size_t> at = index_of(key))
                {
                    set_header(*at, value);
                }
            }));
    }
    if (observer != nullptr)
    {
        set_parent_of(*observer, this);
    }
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index), std::move(tab));
    if (selected_index_ == detail::NoIndex)
    {
        selected_index_ = 0;
    }
    else if (index <= selected_index_)
    {
        ++selected_index_;
    }
    if (cursor_index_ != detail::NoIndex && index <= cursor_index_)
    {
        ++cursor_index_;
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_tabs_changed_)
    {
        on_tabs_changed_();
    }
    return observer;
}

void TabsControl::remove_tab(std::size_t index)
{
    if (index >= tabs_.size())
    {
        return;
    }
    // A close with the pointer in the strip keeps the Equal width
    // until the pointer leaves, so the next tab's close button lands where this one's was.
    if (width_mode_ == TabsWidthMode::Equal && pointer_in_strip_ && !tabs_.empty())
    {
        frozen_tab_width_ = tabs_[index].width;
    }
    const std::size_t count_before = tabs_.size();
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    hovered_index_ = pressed_index_ = close_hovered_ = close_pressed_ = detail::NoIndex;
    dragging_                                                         = false;

    // Removing the selected tab selects the one now at its index, or the last.
    bool moved_selection = false;
    if (tabs_.empty())
    {
        selected_index_ = detail::NoIndex;
        moved_selection = true;
    }
    else if (index == selected_index_)
    {
        selected_index_ = std::min(index, tabs_.size() - 1);
        moved_selection = true;
    }
    else if (index < selected_index_ && selected_index_ != detail::NoIndex)
    {
        --selected_index_;
    }
    if (cursor_index_ != detail::NoIndex)
    {
        cursor_index_ = tabs_.empty()
                            ? detail::NoIndex
                            : std::min(cursor_index_ > index ? cursor_index_ - 1 : cursor_index_, tabs_.size() - 1);
    }
    (void)count_before;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_tabs_changed_)
    {
        on_tabs_changed_();
    }
    if (moved_selection && on_selection_changed_)
    {
        on_selection_changed_(selected_key());
    }
}

void TabsControl::remove_tab(std::string_view key)
{
    remove_tab(detail::index_or_none(index_of(key)));
}

void TabsControl::move_tab(std::size_t from, std::size_t to)
{
    if (from >= tabs_.size() || to >= tabs_.size() || from == to)
    {
        return;
    }
    Tab tab = std::move(tabs_[from]);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to), std::move(tab));
    const auto remap = [from, to](std::size_t i)
    {
        if (i == detail::NoIndex)
        {
            return i;
        }
        if (i == from)
        {
            return to;
        }
        if (from < to && i > from && i <= to)
        {
            return i - 1;
        }
        if (to < from && i >= to && i < from)
        {
            return i + 1;
        }
        return i;
    };
    selected_index_ = remap(selected_index_);
    cursor_index_   = remap(cursor_index_);
    pressed_index_  = remap(pressed_index_);
    hovered_index_  = remap(hovered_index_);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_tabs_changed_)
    {
        on_tabs_changed_();
    }
}

const std::string& TabsControl::header(std::size_t index) const
{
    static const std::string empty;
    return index < tabs_.size() ? tabs_[index].header : empty;
}

std::string TabsControl::unique_key(const std::string& header) const
{
    // Two "Untitled" tabs get "Untitled" and "Untitled (2)", so a close request names one of them.
    std::string key = header;
    for (int n = 2; index_of(key); ++n)
    {
        key = std::format("{} ({})", header, n);
    }
    return key;
}

const std::string& TabsControl::key(std::size_t index) const
{
    static const std::string empty;
    return index < tabs_.size() ? tabs_[index].key : empty;
}

std::optional<std::size_t> TabsControl::index_of(std::string_view key) const
{
    const auto it = std::ranges::find(tabs_, key, &Tab::key);
    if (it == tabs_.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - tabs_.begin());
}

void TabsControl::set_header(std::size_t index, std::string header)
{
    if (index < tabs_.size())
    {
        tabs_[index].header = std::move(header);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void TabsControl::set_icon(std::size_t index, IconSource icon)
{
    if (index < tabs_.size())
    {
        tabs_[index].icon = icon;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

bool TabsControl::closable(std::size_t index) const
{
    return index < tabs_.size() && tabs_[index].closable;
}

void TabsControl::set_closable(std::size_t index, bool closable)
{
    if (index < tabs_.size())
    {
        tabs_[index].closable = closable;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

Element* TabsControl::content(std::size_t index) const
{
    return index < tabs_.size() ? tabs_[index].content.get() : nullptr;
}

// ---- selection ----

void TabsControl::set_selected_index(std::size_t index)
{
    if (tabs_.empty())
    {
        return;
    }
    select(std::min(index, tabs_.size() - 1));
}

void TabsControl::set_selected_key(std::string_view key)
{
    select(detail::index_or_none(index_of(key)));
}

void TabsControl::select(std::size_t index)
{
    if (index >= tabs_.size() || index == selected_index_)
    {
        return;
    }
    selected_index_ = index;
    cursor_index_   = index;
    bring_into_view(index);
    // The new tab's content may want a different size, so this is a layout change.
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected_key());
    }
}

void TabsControl::request_close(std::size_t index)
{
    if (index >= tabs_.size() || !tabs_[index].closable)
    {
        return;
    }
    if (on_tab_close_requested_)
    {
        on_tab_close_requested_(tabs_[index].key);
        return;
    }
    remove_tab(index);
}

void TabsControl::set_add_tab_button_visible(std::optional<bool> visible)
{
    add_visible_ = visible;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TabsControl::on_add_tab_button_click(Callback<void()> handler)
{
    on_add_tab_button_click_ = std::move(handler);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); // the "+" may come or go
}

void TabsControl::enabled_changed()
{
    add_button_->set_enabled(is_enabled());
    if (!is_enabled())
    {
        hovered_index_ = pressed_index_ = close_hovered_ = close_pressed_ = detail::NoIndex;
        scroll_hovered_ = scroll_pressed_ = 0;
        dragging_                         = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void TabsControl::focus_changed(bool /*focused*/)
{
    if (is_focused() && (cursor_index_ == detail::NoIndex || cursor_index_ >= tabs_.size()))
    {
        cursor_index_ = selected_index_;
    }
    mark_dirty(DirtyFlags::Visual);
}

bool TabsControl::close_button_shown(std::size_t index) const
{
    // A closable tab shows its close button always, or with OnPointerOver only while selected or hovered.
    if (index >= tabs_.size() || !tabs_[index].closable)
    {
        return false;
    }
    if (close_mode_ == TabsCloseButtonOverlayMode::OnPointerOver)
    {
        return index == selected_index_ || index == hovered_index_;
    }
    return true;
}

// ---- layout ----

TextStyle TabsControl::header_style(const Theme& theme, bool selected)
{
    TextStyle style  = theme.text_style;
    style.font_size  = TabsHeaderFontSize;
    style.horizontal = TextAlign::Leading;
    // A selected tab's label is SemiBold whether hovered or pressed. An unselected one stays
    // Regular.
    style.weight = selected ? FontWeight::SemiBold : FontWeight::Regular;
    return style;
}

float TabsControl::natural_width(std::size_t index) const
{
    const Tab& tab      = tabs_[index];
    const bool has_icon = !tab.icon.empty();
    // Room for the close button stays whenever the tab is closable: OnPointerOver's hidden button
    // still occupies its column, and the padding only changes with `closable`.
    const bool  closable = tab.closable;
    const float trail    = closable ? CloseGap + CloseWidth + PadRightClose : PadRightNoClose;
    if (compact_tab(index))
    {
        // Compact: the icon alone, no gap, no header.
        return 2.0f * Border + PadLeft + TabsIconSize + trail;
    }
    const float text = index == selected_index_ ? tab.text_width_selected : tab.text_width;
    return 2.0f * Border + PadLeft + (has_icon ? TabsIconSize + TabsIconGap : 0.0f) + text + trail;
}

Size TabsControl::measure_override(Size available, const MeasureContext& context) const
{
    float line_height = context.measurer.measure("Ag", header_style(context.theme, false)).height;
    for (const Tab& tab : tabs_)
    {
        const Size normal       = context.measurer.measure(tab.header, header_style(context.theme, false));
        const Size bold         = context.measurer.measure(tab.header, header_style(context.theme, true));
        tab.text_width          = std::ceil(normal.width);
        tab.text_width_selected = std::ceil(bold.width);
        line_height             = std::max(line_height, bold.height);
    }
    item_height_ = std::max({ItemMinHeight, line_height + 2.0f * (PadV + Border), CloseHeight + 2.0f * (PadV + Border)});
    strip_height_ = HeaderTopPadding + item_height_;

    (void)add_tip_->measure(Size{AddButtonWidth, AddButtonHeight}, context);
    (void)hover_tip_->measure(Size{Unbounded, Unbounded}, context);
    const auto visible = [&](const std::unique_ptr<Element>& el) -> Size
    { return el ? el->measure(Size{Unbounded, strip_height_}, context) : Size{}; };
    header_desired_ = visible(strip_header_);
    footer_desired_ = visible(strip_footer_);

    float tabs_width = ListPadLeft + 2.0f * ListEdge;
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        tabs_width += width_mode_ == TabsWidthMode::Equal ? ItemMaxWidth : natural_width(i);
    }
    const float strip_width = std::max(2.0f, header_desired_.width) + tabs_width +
                              (is_add_tab_button_visible() ? AddPadLeft + AddButtonWidth : 0.0f) + footer_desired_.width;

    const auto& era = detail::controls(context.theme).era_tabs;
    page_inset_     = era ? Border + era->page_padding : 0.0f;
    const Size content_available{std::isfinite(available.width) ? std::max(0.0f, available.width - 2.0f * page_inset_) : 0.0f,
                                 std::max(0.0f, available.height - strip_height_ - 2.0f * page_inset_)};
    Size content_desired{};
    if (Element* c = content(selected_index_); c != nullptr)
    {
        content_desired = c->measure(content_available, context);
    }

    Size desired{std::max(strip_width, content_desired.width + 2.0f * page_inset_),
                 strip_height_ + content_desired.height + 2.0f * page_inset_};
    if (std::isfinite(available.width))
    {
        desired.width = available.width;
    }
    if (std::isfinite(available.height))
    {
        desired.height = available.height;
    }
    return desired;
}

void TabsControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const float       x0      = final_rect.x;
    const float       y0      = final_rect.y;
    const float       bottom  = y0 + strip_height_;
    const float       left_w  = std::max(2.0f, header_desired_.width); // the left content column is at least 2 wide
    const float       add_w   = is_add_tab_button_visible() ? AddPadLeft + AddButtonWidth : 0.0f;
    const float       right_w = footer_desired_.width;
    const float       room    = std::max(0.0f, final_rect.width - left_w - add_w - right_w);
    const float       edges   = ListPadLeft + 2.0f * ListEdge;
    const std::size_t n       = tabs_.size();

    // The tab widths.
    overflow_ = false;
    if (width_mode_ == TabsWidthMode::Equal && n > 0)
    {
        float width = frozen_tab_width_.value_or(
            std::clamp((room - edges) / static_cast<float>(n), ItemMinWidth, ItemMaxWidth));
        if (width * static_cast<float>(n) + edges > room)
        {
            overflow_ = true;
            // Recomputed against the viewport between the scroll buttons, without filling the space.
            if (!frozen_tab_width_)
            {
                width = std::clamp((room - edges - 2.0f * ScrollContainer) / static_cast<float>(n), ItemMinWidth, ItemMaxWidth);
            }
        }
        for (Tab& tab : tabs_)
        {
            tab.width = width;
        }
    }
    else
    {
        float total = edges;
        for (std::size_t i = 0; i < n; ++i)
        {
            tabs_[i].width = natural_width(i);
            total += tabs_[i].width;
        }
        overflow_ = total > room;
    }
    list_width_ = edges - ListPadLeft;
    for (const Tab& tab : tabs_)
    {
        list_width_ += tab.width;
    }

    const float tabs_col_x = x0 + left_w;
    const float tabs_col_w = overflow_ ? room : std::min(room, list_width_ + ListPadLeft);
    viewport_ = overflow_
                    ? Rect{tabs_col_x + ScrollContainer, y0, std::max(0.0f, tabs_col_w - 2.0f * ScrollContainer), strip_height_}
                    : Rect{tabs_col_x, y0, tabs_col_w, strip_height_};
    set_scroll_offset(scroll_offset_);

    if (strip_header_)
    {
        strip_header_->arrange(Rect{x0, y0, left_w, strip_height_});
    }
    const float add_x = tabs_col_x + tabs_col_w;
    if (is_add_tab_button_visible())
    {
        add_tip_->arrange(
            Rect{add_x + AddPadLeft, bottom - AddPadBottom - AddButtonHeight, AddButtonWidth, AddButtonHeight});
    }
    else
    {
        add_tip_->arrange(Rect{add_x, y0, 0.0f, 0.0f});
    }
    if (strip_footer_)
    {
        const float fx = add_x + add_w;
        strip_footer_->arrange(Rect{fx, y0, std::max(0.0f, final_rect.right() - fx), strip_height_});
    }

    if (Element* c = content(selected_index_); c != nullptr)
    {
        c->arrange(Rect{x0, y0 + strip_height_, final_rect.width, std::max(0.0f, final_rect.height - strip_height_)}.inset(
            Thickness{page_inset_}));
    }
    place_item_automation();
}

float TabsControl::tab_x(std::size_t index) const
{
    float x = ListPadLeft + ListEdge;
    for (std::size_t i = 0; i < index && i < tabs_.size(); ++i)
    {
        x += tabs_[i].width;
    }
    return x;
}

std::optional<Rect> TabsControl::tab_bounds(std::size_t index) const
{
    if (index >= tabs_.size())
    {
        return std::nullopt;
    }
    const Rect slot{viewport_.x + tab_x(index) - scroll_offset_, bounds().y + HeaderTopPadding, tabs_[index].width, item_height_};
    if (slot.right() <= viewport_.x || slot.x >= viewport_.right())
    {
        return std::nullopt;
    }
    return slot;
}

bool TabsControl::takes_focus_at(Point p) const
{
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        if (const auto slot = tab_bounds(i); slot && intersection(*slot, viewport_).contains(p))
        {
            return true;
        }
    }
    return false;
}

std::optional<Rect> TabsControl::close_button_bounds(std::size_t index) const
{
    const auto slot = tab_bounds(index);
    if (!slot || !close_button_shown(index))
    {
        return std::nullopt;
    }
    return Rect{slot->right() - Border - PadRightClose - CloseWidth,
                slot->y + (slot->height - CloseHeight) * 0.5f,
                CloseWidth,
                CloseHeight};
}

std::optional<Rect> TabsControl::scroll_decrease_bounds() const
{
    if (!overflow_)
    {
        return std::nullopt;
    }
    return Rect{viewport_.x - ScrollInnerPad - ScrollButtonWidth,
                bounds().y + strip_height_ - ScrollBottomPad - ScrollButtonHeight,
                ScrollButtonWidth,
                ScrollButtonHeight};
}

std::optional<Rect> TabsControl::scroll_increase_bounds() const
{
    if (!overflow_)
    {
        return std::nullopt;
    }
    return Rect{viewport_.right() + ScrollInnerPad,
                bounds().y + strip_height_ - ScrollBottomPad - ScrollButtonHeight,
                ScrollButtonWidth,
                ScrollButtonHeight};
}

void TabsControl::set_scroll_offset(float offset)
{
    const float max_offset = std::max(0.0f, list_width_ + ListPadLeft - viewport_.width);
    const float clamped    = overflow_ ? std::clamp(offset, 0.0f, max_offset) : 0.0f;
    if (clamped != scroll_offset_)
    {
        scroll_offset_ = clamped;
        mark_dirty(DirtyFlags::Visual);
    }
}

void TabsControl::bring_into_view(std::size_t index)
{
    // Scroll the strip so the tab is in view.
    if (!overflow_ || index >= tabs_.size())
    {
        return;
    }
    const float left  = tab_x(index);
    const float right = left + tabs_[index].width;
    if (left < scroll_offset_)
    {
        set_scroll_offset(left - ListEdge - ListPadLeft);
    }
    else if (right > scroll_offset_ + viewport_.width)
    {
        set_scroll_offset(right + ListEdge - viewport_.width);
    }
}

std::size_t TabsControl::tab_at(Point p) const
{
    if (!viewport_.contains(p))
    {
        return detail::NoIndex;
    }
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        if (const auto slot = tab_bounds(i); slot && slot->contains(p))
        {
            return i;
        }
    }
    return detail::NoIndex;
}

// ---- render ----

void TabsControl::render_tab(Renderer& renderer, const Theme& theme, std::size_t i, const Rect& slot) const
{
    const Tab& tab      = tabs_[i];
    const bool selected = i == selected_index_;
    const bool hovered  = i == hovered_index_ && close_hovered_ != i;
    const bool pressed  = i == pressed_index_ && i == hovered_index_ && close_pressed_ != i;
    const bool fluent   = detail::controls(theme).tab_layer_surfaces;

    Color text_color = selected ? theme.colors().field_text : theme.colors().text;
    if (const auto& era = detail::controls(theme).era_tabs)
    {
        // The Aero tab: the selected tab grows 2 at each side and the top and 1 down over
        // the page's border, in the page's color; the others sit 2 lower with the gray face, blue
        // under the pointer. A border on three sides, none along the bottom.
        const float grow = era->selected_grow;
        const Rect  face = selected ? Rect{slot.x - grow, slot.y, slot.width + 2.0f * grow, slot.height + 1.0f}
                                    : Rect{slot.x, slot.y + grow, slot.width, std::max(0.0f, slot.height - grow)};
        const bool  lit  = is_enabled() && !selected && (hovered || pressed);
        if (!is_enabled())
        {
            renderer.fill_rounded_rect(face, 0.0f, era->disabled_face);
        }
        else if (selected)
        {
            renderer.fill_rounded_rect(face, 0.0f, era->page);
        }
        else
        {
            renderer.fill_rounded_rect_gradient(face,
                                                0.0f,
                                                LinearGradientBrush{lit ? era->hover_face : era->face, GradientAxis::Vertical});
        }
        const Color edge = !is_enabled() ? era->disabled_border : lit ? era->hover_border : era->border;
        renderer.fill_rounded_rect(Rect{face.x, face.y, face.width, 1.0f}, 0.0f, edge);
        renderer.fill_rounded_rect(Rect{face.x, face.y, 1.0f, face.height}, 0.0f, edge);
        renderer.fill_rounded_rect(Rect{face.right() - 1.0f, face.y, 1.0f, face.height}, 0.0f, edge);
        text_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
    }
    else if (fluent)
    {
        if (selected)
        {
            // The selected tab's background inside its border, with the flares carrying it into
            // the rule: one shape, filled and outlined.
            std::vector<Point> shape = selected_tab_shape(slot, theme.overlay_corner_radius);
            renderer.fill_polygon(FillPolygon{shape, theme.palette.solid_background_tertiary});
            renderer.stroke_polyline(StrokePolyline{std::move(shape), theme.palette.card_stroke_default, 1.0f, false});
        }
        else if (is_enabled() && (hovered || pressed))
        {
            // The secondary alt layer fill on hover, the default one on press.
            const Color fill = pressed ? theme.palette.on_backdrop_pressed : theme.palette.on_backdrop_hover;
            fill_top_rounded(renderer, slot, theme.overlay_corner_radius, fill);
        }
        // The header and icon color: secondary text at rest and on hover, tertiary pressed,
        // primary selected, disabled disabled.
        text_color = !is_enabled() ? theme.palette.text_fill_disabled
                     : selected    ? theme.palette.text_fill_primary
                     : pressed     ? theme.palette.text_fill_tertiary
                                   : theme.palette.text_fill_secondary;
    }
    else if (selected)
    {
        // A selected tab reads as the same surface as the content below it.
        renderer.fill_rounded_rect(slot, 0.0f, theme.colors().field);
    }
    else if (hovered)
    {
        renderer.fill_rounded_rect(slot, 0.0f, theme.colors().control_fill_hover.with_alpha(TabsHoverOverlayAlpha));
    }
    if (!is_enabled() && !fluent && !detail::controls(theme).era_tabs)
    {
        text_color = theme.colors().text_disabled;
    }

    float       x         = slot.x + Border + PadLeft;
    const float close_col = tab.closable ? CloseGap + CloseWidth + PadRightClose : PadRightNoClose;
    const float right     = slot.right() - Border - close_col;
    const bool  has_icon  = !tab.icon.empty();
    if (has_icon)
    {
        const Rect icon{x, slot.y + (slot.height - TabsIconSize) * 0.5f, TabsIconSize, TabsIconSize};
        tab.icon.draw_in_slot(renderer, icon, text_color, TabsIconSize, theme.icon_font_family);
        x += TabsIconSize + (compact_tab(i) ? 0.0f : TabsIconGap);
    }
    if (!compact_tab(i))
    {
        const Rect text{x, slot.y, std::max(0.0f, right - x), slot.height};
        renderer.push_clip(text);
        renderer.draw_text(tab.header, text, text_color, header_style(theme, selected));
        renderer.pop_clip();
    }

    if (const auto close = close_button_bounds(i))
    {
        // The close button: the secondary subtle fill on hover, the tertiary one pressed. The
        // glyph is the primary text color, secondary pressed.
        const bool close_hot  = close_hovered_ == i;
        const bool close_down = close_pressed_ == i && close_hot;
        Color      glyph      = fluent ? theme.palette.text_fill_primary : text_color;
        if (fluent && is_enabled() && (close_hot || close_down))
        {
            renderer.fill_rounded_rect(*close,
                                       theme.corner_radius,
                                       close_down ? theme.palette.subtle_fill_tertiary : theme.palette.subtle_fill_secondary);
            glyph = close_down ? theme.palette.text_fill_secondary : glyph;
        }
        else if (!fluent && is_enabled() && close_hot)
        {
            renderer.fill_rounded_rect(*close, 0.0f, theme.colors().control_fill_hover.with_alpha(TabsHoverOverlayAlpha));
        }
        if (!is_enabled())
        {
            glyph = theme.colors().text_disabled;
        }
        draw_icon(renderer, TabsCloseGlyph, *close, glyph, CloseGlyphSize, theme.icon_font_family);
    }

    // The separator: hidden for the selected tab, for the tab immediately left of it, and while
    // this tab is itself hovered or pressed.
    const bool separator_hidden = selected || (selected_index_ != detail::NoIndex && i + 1 == selected_index_) ||
                                  i == hovered_index_;
    if (fluent && !separator_hidden)
    {
        renderer.fill_rounded_rect(Rect{slot.right() - 1.0f,
                                        slot.y + SeparatorInsetV,
                                        1.0f,
                                        std::max(0.0f, slot.height - 2.0f * SeparatorInsetV)},
                                   0.0f,
                                   theme.colors().divider);
    }
}

void TabsControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const Rect   strip = strip_bounds();
    if (const std::optional<Color> fill = strip_background_.resolve(theme))
    {
        renderer.fill_rounded_rect(strip, 0.0f, *fill);
    }

    // The strip's rule along the bottom: everywhere but under the selected tab and its flares.
    std::optional<Rect> selected_slot = tab_bounds(selected_index_);
    const auto&         era           = detail::controls(theme).era_tabs;
    if (era)
    {
        // The page the selected tab merges into, framed, its top edge the strip's bottom row.
        const Rect page{bounds().x, strip.bottom(), bounds().width, std::max(0.0f, bounds().bottom() - strip.bottom())};
        renderer.fill_rounded_rect(page, 0.0f, era->page);
        stroke_rounded_rect_inside(renderer, page, 0.0f, era->border, Border);
    }
    if (detail::controls(theme).tab_layer_surfaces)
    {
        const float y     = strip.bottom() - 1.0f;
        const Color color = theme.palette.card_stroke_default;
        if (selected_slot)
        {
            const float gap_left  = std::max(viewport_.x, selected_slot->x - Flare);
            const float gap_right = std::min(viewport_.right(), selected_slot->right() + Flare);
            renderer.fill_rounded_rect(Rect{strip.x, y, std::max(0.0f, gap_left - strip.x), 1.0f}, 0.0f, color);
            renderer.fill_rounded_rect(Rect{gap_right, y, std::max(0.0f, strip.right() - gap_right), 1.0f}, 0.0f, color);
        }
        else
        {
            renderer.fill_rounded_rect(Rect{strip.x, y, strip.width, 1.0f}, 0.0f, color);
        }
    }

    // An era's selected tab reaches 1 below the strip, over the page's top border.
    renderer.push_clip(era ? Rect{viewport_.x, viewport_.y, viewport_.width, viewport_.height + 1.0f} : viewport_);
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        if (i == selected_index_)
        {
            continue; // drawn last, over its neighbors' separators and rule ends
        }
        if (const auto slot = tab_bounds(i))
        {
            render_tab(renderer, theme, i, *slot);
        }
    }
    if (selected_slot)
    {
        render_tab(renderer, theme, selected_index_, *selected_slot);
    }
    renderer.pop_clip();

    // The scroll buttons: transparent, the secondary subtle fill on hover, the tertiary one
    // pressed, the glyph the secondary text color, disabled at the ends.
    const auto scroll_button = [&](const std::optional<Rect>& rect, int which, const IconGlyph& glyph, bool can)
    {
        if (!rect)
        {
            return;
        }
        const bool hot  = scroll_hovered_ == which && can && is_enabled();
        const bool down = scroll_pressed_ == which && hot;
        if (hot)
        {
            const Color fill = detail::controls(theme).tab_layer_surfaces
                                   ? (down ? theme.palette.subtle_fill_tertiary : theme.palette.subtle_fill_secondary)
                                   : theme.colors().control_fill_hover.with_alpha(TabsHoverOverlayAlpha);
            renderer.fill_rounded_rect(*rect, theme.corner_radius, fill);
        }
        const Color color = !can || !is_enabled()
                                ? theme.colors().text_disabled
                                : (detail::controls(theme).tab_layer_surfaces ? theme.palette.text_fill_secondary
                                                                              : theme.colors().text);
        draw_icon(renderer, glyph, *rect, color, glyph.size, theme.icon_font_family);
    };
    const float max_offset = std::max(0.0f, list_width_ + ListPadLeft - viewport_.width);
    scroll_button(scroll_decrease_bounds(), -1, ScrollLeftGlyph, scroll_offset_ > 0.0f);
    scroll_button(scroll_increase_bounds(), +1, ScrollRightGlyph, scroll_offset_ < max_offset);

    if (is_add_tab_button_visible())
    {
        add_tip_->render(renderer, context);
    }
    if (strip_header_)
    {
        strip_header_->render(renderer, context);
    }
    if (strip_footer_)
    {
        strip_footer_->render(renderer, context);
    }

    if (Element* c = content(selected_index_); c != nullptr)
    {
        c->render(renderer, context);
    }
}

Rect TabsControl::focus_ring_rect(const Theme& /*theme*/) const
{
    const auto slot = tab_bounds(cursor_index_);
    return slot ? intersection(*slot, viewport_) : Rect{};
}

// ---- input ----

bool TabsControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    const Rect        strip      = strip_bounds();
    const std::size_t over       = tab_at(e.position);
    std::size_t       over_close = detail::NoIndex;
    if (over != detail::NoIndex)
    {
        if (const auto close = close_button_bounds(over); close && close->contains(e.position))
        {
            over_close = over;
        }
    }
    int over_scroll = 0;
    if (const auto r = scroll_decrease_bounds(); r && r->contains(e.position))
    {
        over_scroll = -1;
    }
    else if (const auto r2 = scroll_increase_bounds(); r2 && r2->contains(e.position))
    {
        over_scroll = +1;
    }

    const bool was_in_strip = pointer_in_strip_;
    pointer_in_strip_       = e.action != PointerAction::Leave && strip.contains(e.position);
    if (was_in_strip && !pointer_in_strip_ && frozen_tab_width_)
    {
        frozen_tab_width_.reset(); // the pointer left the strip: the held widths catch up
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            if (pressed_index_ != detail::NoIndex && can_reorder_ && close_pressed_ == detail::NoIndex)
            {
                if (!dragging_ && std::abs(e.position.x - press_point_.x) > TabsDragThreshold)
                {
                    dragging_ = true;
                }
                if (dragging_)
                {
                    // Reorder: the dragged tab takes the slot of whichever neighbor's center the
                    // pointer crosses.
                    std::size_t target = pressed_index_;
                    while (target + 1 < tabs_.size())
                    {
                        const auto next = tab_bounds(target + 1);
                        if (!next || e.position.x < next->center().x)
                        {
                            break;
                        }
                        ++target;
                    }
                    while (target > 0)
                    {
                        const auto prev = tab_bounds(target - 1);
                        if (!prev || e.position.x > prev->center().x)
                        {
                            break;
                        }
                        --target;
                    }
                    if (target != pressed_index_)
                    {
                        move_tab(pressed_index_, target);
                    }
                }
            }
            if (over != hovered_index_ || over_close != close_hovered_ || over_scroll != scroll_hovered_)
            {
                hovered_index_  = over;
                close_hovered_  = over_close;
                scroll_hovered_ = over_scroll;
                mark_dirty(DirtyFlags::Visual);
            }
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                if (over_scroll != 0)
                {
                    scroll_pressed_   = over_scroll;
                    scroll_repeat_ms_ = 0.0;
                    set_scroll_offset(scroll_offset_ + static_cast<float>(over_scroll) * ScrollAmount);
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                if (over_close != detail::NoIndex)
                {
                    close_pressed_ = over_close;
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                if (over != detail::NoIndex)
                {
                    // A tab has a pressed look, selected or not. Selection lands on Down,
                    // so the pressed look only lasts until the button comes back up.
                    pressed_index_ = over;
                    press_point_   = e.position;
                    dragging_      = false;
                    hover_tip_->close();
                    select(over);
                    return true;
                }
            }
            else if (e.button == PointerButton::Middle && over != detail::NoIndex)
            {
                // A middle click closes the tab.
                request_close(over);
                return true;
            }
            if (strip.contains(e.position))
            {
                return true; // a strip click never also reaches the content underneath
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                if (close_pressed_ != detail::NoIndex)
                {
                    const std::size_t index = close_pressed_;
                    close_pressed_          = detail::NoIndex;
                    mark_dirty(DirtyFlags::Visual);
                    if (over_close == index)
                    {
                        request_close(index);
                    }
                    return true;
                }
                if (scroll_pressed_ != 0)
                {
                    scroll_pressed_ = 0;
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                if (pressed_index_ != detail::NoIndex)
                {
                    handled        = true;
                    pressed_index_ = detail::NoIndex;
                    dragging_      = false;
                    mark_dirty(DirtyFlags::Visual);
                }
            }
            break;

        case PointerAction::Leave:
            hovered_index_ = close_hovered_ = close_pressed_ = pressed_index_ = detail::NoIndex;
            scroll_hovered_ = scroll_pressed_ = 0;
            dragging_                         = false;
            mark_dirty(DirtyFlags::Visual);
            break;

        default:
            break;
    }

    // The tab's header (placed at the mouse), the close button's "Close tab (Ctrl+F4)", or the
    // scroll buttons' names.
    int         key = -1;
    std::string text;
    Rect        anchor{};
    if (over_scroll == -1)
    {
        key    = TipScrollBackward;
        text   = strings().tabs.scroll_backward;
        anchor = *scroll_decrease_bounds();
    }
    else if (over_scroll == +1)
    {
        key    = TipScrollForward;
        text   = strings().tabs.scroll_forward;
        anchor = *scroll_increase_bounds();
    }
    else if (over_close != detail::NoIndex)
    {
        key  = static_cast<int>(over_close) * 2 + 1;
        text = detail::fill(strings().common.with_shortcut, {strings().tabs.close_tab, shortcut_text(Ctrl + Key::F4)});
        anchor = *close_button_bounds(over_close);
    }
    else if (over != detail::NoIndex && !tabs_[over].header.empty())
    {
        key    = static_cast<int>(over) * 2;
        text   = tabs_[over].header;
        anchor = *tab_bounds(over);
    }
    if (key != hover_tip_key_)
    {
        hover_tip_->close();
        hover_tip_key_ = key;
        if (auto* block = dynamic_cast<TextControl*>(hover_tip_->content()); block != nullptr)
        {
            block->set_text(text);
        }
        hover_tip_->arrange(key == -1 ? Rect{bounds().x, bounds().y, 0.0f, 0.0f} : anchor);
    }
    if (hover_tip_key_ != -1)
    {
        hover_tip_->handle_pointer(e); // just placed over the tab: it starts timing with this event
    }
    return handled;
}

void TabsControl::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    for (Element* el : {static_cast<Element*>(add_tip_.get()),
                        hover_tip_key_ != -1 ? static_cast<Element*>(hover_tip_.get()) : nullptr,
                        strip_header_.get(),
                        strip_footer_.get(),
                        content(selected_index_)})
    {
        if (el != nullptr)
        {
            out.push_back(el);
        }
    }
}

bool TabsControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    if (strip_bounds().contains(e.position))
    {
        // The strip's Scroll takes the wheel only while it can scroll that way; a strip with
        // nothing to scroll, or one already at that end, chains it to the page around the Tabs.
        const float before = scroll_offset_;
        if (overflow_)
        {
            set_scroll_offset(scroll_offset_ + e.wheel_delta);
        }
        return scroll_offset_ != before;
    }
    return false; // the content had the turn first
}

bool TabsControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down || tabs_.empty())
    {
        return handled;
    }
    if (cursor_index_ >= tabs_.size())
    {
        cursor_index_ = selected_index_ < tabs_.size() ? selected_index_ : 0;
    }
    switch (e.key)
    {
        // The arrow keys move the focus, and the selection stays.
        case Key::ArrowLeft:
            if (cursor_index_ > 0)
            {
                --cursor_index_;
                bring_into_view(cursor_index_);
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case Key::ArrowRight:
            if (cursor_index_ + 1 < tabs_.size())
            {
                ++cursor_index_;
                bring_into_view(cursor_index_);
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case Key::Home:
            cursor_index_ = 0;
            bring_into_view(cursor_index_);
            mark_dirty(DirtyFlags::Visual);
            break;
        case Key::End:
            cursor_index_ = tabs_.size() - 1;
            bring_into_view(cursor_index_);
            mark_dirty(DirtyFlags::Visual);
            break;
        case Key::Enter:
        case Key::Space:
            select(cursor_index_);
            break;
        default:
            break;
    }
    return handled;
}

bool TabsControl::handle_shortcut(const KeyEvent& e)
{
    if (is_enabled() && e.modifiers.ctrl && !tabs_.empty())
    {
        if (e.key == Key::Tab)
        {
            // Ctrl+Tab / Ctrl+Shift+Tab move the selection, wrapping.
            const std::size_t n     = tabs_.size();
            const std::size_t from  = selected_index_ < n ? selected_index_ : 0;
            const std::size_t index = e.modifiers.shift ? (from + n - 1) % n : (from + 1) % n;
            select(index);
            return true;
        }
        if (e.key == Key::F4 && !e.modifiers.shift)
        {
            // Ctrl+F4 asks to close the selected tab.
            if (closable(selected_index_))
            {
                request_close(selected_index_);
                return true;
            }
            return false;
        }
    }
    return Element::handle_shortcut(e);
}

// ---- focus ----

void TabsControl::collect_focusables(std::vector<Element*>& out)
{
    if (focusable())
    {
        out.push_back(this);
    }
    if (strip_header_)
    {
        strip_header_->collect_focusables(out);
    }
    if (is_add_tab_button_visible())
    {
        add_tip_->collect_focusables(out);
    }
    if (strip_footer_)
    {
        strip_footer_->collect_focusables(out);
    }
    if (Element* c = content(selected_index_); c != nullptr)
    {
        c->collect_focusables(out);
    }
}

// ---- tree/invalidation plumbing ----

void TabsControl::for_each_child(ChildVisitor visit) const
{
    for (Element* el : {static_cast<Element*>(add_tip_.get()),
                        static_cast<Element*>(hover_tip_.get()),
                        strip_header_.get(),
                        strip_footer_.get(),
                        content(selected_index_)})
    {
        if (el != nullptr)
        {
            visit(*el);
        }
    }
}

void TabsControl::sync_native(NativeHost& host)
{
    for (Element* el : {strip_header_.get(), strip_footer_.get(), content(selected_index_)})
    {
        if (el != nullptr)
        {
            el->sync_native(host);
        }
    }
}

void TabsControl::tick_override(Duration delta)
{
    // The scroll buttons repeat: held, they scroll again after the delay and then every
    // interval.
    if (scroll_pressed_ != 0 && scroll_hovered_ == scroll_pressed_)
    {
        const double before = scroll_repeat_ms_;
        scroll_repeat_ms_ += delta.count();
        if (scroll_repeat_ms_ >= RepeatDelayMs)
        {
            const auto ticks_before = before < RepeatDelayMs
                                          ? -1
                                          : static_cast<int>((before - RepeatDelayMs) / TabsRepeatIntervalMs);
            const auto ticks_now    = static_cast<int>((scroll_repeat_ms_ - RepeatDelayMs) / TabsRepeatIntervalMs);
            for (int i = ticks_before; i < ticks_now; ++i)
            {
                set_scroll_offset(scroll_offset_ + static_cast<float>(scroll_pressed_) * ScrollAmount);
            }
        }
    }
}

void TabsControl::collect_live(std::vector<Element*>& out)
{
    out.push_back(add_tip_.get());
    out.push_back(hover_tip_.get());
    for (Element* el : {strip_header_.get(), strip_footer_.get(), content(selected_index_)})
    {
        if (el != nullptr)
        {
            detail::TreeAccess::collect_live(*el, out);
        }
    }
}

void TabsControl::automation_children(std::vector<Element*>& out) const
{
    if (strip_header_)
    {
        out.push_back(strip_header_.get());
    }
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        out.push_back(item_automation(i));
    }
    if (is_add_tab_button_visible())
    {
        out.push_back(add_tip_.get());
    }
    for (Element* el : {strip_footer_.get(), content(selected_index_)})
    {
        if (el != nullptr)
        {
            out.push_back(el);
        }
    }
}

Element* TabsControl::add_tab(ui::Tab tab)
{
    return insert_tab(tabs_.size(), std::move(tab));
}

std::unique_ptr<ui::Element> Tabs::build() &&
{
    auto node = std::make_unique<ui::TabsControl>();
    for (auto& tab : items.entries)
    {
        node->add_tab(ui::Tab{.header   = std::move(tab.header),
                              .key      = std::move(tab.key),
                              .icon     = tab.icon,
                              .closable = tab.closable,
                              .content  = std::move(tab.node)});
    }
    if (tab_strip_header_content)
    {
        node->set_tab_strip_header_content(tab_strip_header_content.take());
    }
    if (tab_strip_footer_content)
    {
        node->set_tab_strip_footer_content(tab_strip_footer_content.take());
    }
    apply(can_reorder_tabs, *node, [raw = node.get()](const bool& v) { raw->set_can_reorder_tabs(v); });

    ui::TabsControl* raw = node.get();
    // An empty or unknown key leaves the selection as it is, and a two-way binding then takes the
    // key of the tab actually selected.
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
    apply(tab_width_mode, *node, [raw](const ui::TabsWidthMode& mode) { raw->set_tab_width_mode(mode); });
    apply(close_button_overlay_mode,
          *node,
          [raw](const ui::TabsCloseButtonOverlayMode& mode) { raw->set_close_button_overlay_mode(mode); });
    apply(add_tab_button_visible,
          *node,
          [raw](const std::optional<bool>& value) { raw->set_add_tab_button_visible(value); });
    apply(strip_background, *node, [raw](const ui::Paint& color) { raw->set_strip_background(color); });

    if (auto changed = ui::write_back(selected_writer, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }
    if (on_tab_close_requested)
    {
        node->on_tab_close_requested(std::move(on_tab_close_requested));
    }
    if (on_add_tab_button_click)
    {
        node->on_add_tab_button_click(std::move(on_add_tab_button_click));
    }
    if (on_tabs_changed)
    {
        node->on_tabs_changed(std::move(on_tabs_changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

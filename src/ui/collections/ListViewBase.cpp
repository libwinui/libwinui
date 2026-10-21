// SPDX-License-Identifier: MIT
#include "winui/ui/collections/ListViewBase.hpp"

#include "../menus/ContextMenuPopup.hpp"
#include "../themes/ControlChrome.hpp"
#include "ItemChrome.hpp"
#include "ItemSelection.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/layouts/TileLayout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <any>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui
{

namespace
{

// List row metrics and rounded-mode chrome.
constexpr Thickness ListItemPadding             = Edges{.left = 16.0f, .right = 12.0f};
constexpr float     ListItemMinHeight           = 40.0f; // a row's minimum height
constexpr float     ListItemMinWidth            = 88.0f; // a row's minimum width
constexpr Thickness BackplateMargin             = Symmetric{.horizontal = 4.0f, .vertical = 2.0f};
constexpr float     IndicatorWidth              = 3.0f;
constexpr float     IndicatorMinHeight          = 16.0f;
constexpr float     IndicatorMargin             = 4.0f;
constexpr float     ListViewBaseIndicatorRadius = 1.5f;
constexpr float     IndicatorPressedShrink      = 6.0f;
constexpr float     CheckInlineLeft             = 14.0f; // the check box's left inset
constexpr float     MultiSelectOffset           = 28.0f; // how far the check box pushes the content
constexpr float     ListViewBaseCheckGlyphSize  = 16.0f;
// Tile metrics.
constexpr float     GridItemMin         = 44.0f; // a tile's minimum width and height
constexpr Thickness GridContainerMargin = Edges{.right = 4.0f, .bottom = 4.0f};
constexpr float     GridSelectedBorder  = 2.0f; // a selected tile's border
constexpr float     GridCheckMargin     = 4.0f; // the inner border + the selected border + 1
// The list header.
constexpr Thickness ListViewBaseHeaderPadding   = Edges{.left = 12.0f, .top = 8.0f, .right = 12.0f};
constexpr Thickness ListViewBaseHeaderMargin    = Edges{.bottom = 4.0f};
constexpr float     ListViewBaseHeaderMinHeight = 44.0f;
constexpr float     ListViewBaseHeaderFontSize  = 20.0f; // the header text size
constexpr float     HeaderDividerSpacing        = 8.0f;  // the divider's margin is 12,8,12,0
// SM_CXDRAG/SM_CYDRAG's defaults: how far a press moves before it is a drag.
constexpr float  ListViewBaseDragThreshold = 4.0f;
constexpr double ReorderMs                 = 240.0; // how long a reorder slide takes
// A drag held this close to the start or the end of the view scrolls it, at up to this speed.
constexpr float DragScrollBand  = 56.0f;
constexpr float DragScrollSpeed = 900.0f; // DIPs per second at the rim

TextStyle leading_text(const Theme& theme)
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    return style;
}

} // namespace

// ---- the containers ----

// A list row, a tile or a header: the item chrome around one item's content.
class ListItemContainer final : public Element, private SelectionItemProvider, private InvokeProvider
{
public:
    enum class Role
    {
        Item,
        GroupHeader,
    };

    ListItemContainer(ListViewBase& owner, Role role, std::unique_ptr<Element> content) :
        owner_(owner),
        role_(role),
        content_(std::move(content))
    {
        if (content_)
        {
            set_parent_of(*content_, this);
        }
        apply_style();
    }

    [[nodiscard]] Role role() const
    {
        return role_;
    }
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }
    /// The item this container shows, while it is one of the list's.
    [[nodiscard]] std::optional<std::size_t> item() const
    {
        const std::optional<std::size_t> flat = owner_.repeater_->element_index(*this);
        return flat ? owner_.flat_to_item(*flat) : std::nullopt;
    }
    [[nodiscard]] std::optional<std::size_t> group() const
    {
        const std::optional<std::size_t> flat = owner_.repeater_->element_index(*this);
        return flat ? owner_.flat_to_group(*flat) : std::nullopt;
    }
    void apply_style()
    {
        if (role_ == Role::GroupHeader)
        {
            set_margin(ListViewBaseHeaderMargin);
            set_min_height(ListViewBaseHeaderMinHeight);
            return;
        }
        set_margin(owner_.container_margin_);
        if (owner_.kind_ == ListViewBase::Kind::List)
        {
            set_min_width(ListItemMinWidth);
            set_min_height(owner_.item_height_ ? 0.0f : ListItemMinHeight);
        }
        else
        {
            set_min_width(GridItemMin);
            set_min_height(owner_.item_height_ ? 0.0f : GridItemMin);
        }
        set_height(owner_.item_height_);
    }
    void reset_pointer()
    {
        hovered_ = false;
        pressed_ = false;
        mark_dirty(DirtyFlags::Visual);
    }
    [[nodiscard]] ButtonState state() const
    {
        if (!owner_.is_enabled())
        {
            return ButtonState::Disabled;
        }
        return pressed_ ? ButtonState::Pressed : (hovered_ ? ButtonState::Hover : ButtonState::Normal);
    }
    [[nodiscard]] bool selected() const
    {
        const std::optional<std::size_t> i = item();
        return i && owner_.selection_->is_selected(*i);
    }

    // ---- layout ----
    [[nodiscard]] Thickness padding(const Theme& theme) const
    {
        if (role_ == Role::GroupHeader)
        {
            return Edges{.left   = ListViewBaseHeaderPadding.left,
                         .top    = ListViewBaseHeaderPadding.top,
                         .right  = ListViewBaseHeaderPadding.right,
                         .bottom = ListViewBaseHeaderPadding.bottom + HeaderDividerSpacing + 1.0f};
        }
        if (owner_.kind_ == ListViewBase::Kind::Grid)
        {
            return Thickness{};
        }
        Thickness p = detail::controls(theme).list_item_subtle_selection ? ListItemPadding : theme.padding;
        if (owner_.selection_mode_ == SelectionMode::Multiple)
        {
            p.left += MultiSelectOffset; // the inline check box pushes the content over
        }
        return p;
    }

    /// The list's settings changed what this container looks like or how it lays out its content.
    void invalidate()
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    void render_list_chrome(Renderer& renderer, const Theme& theme) const
    {
        const bool        sel   = selected();
        const ButtonState state = this->state();
        const auto        mode  = owner_.selection_mode_;
        if (detail::controls(theme).list_item_subtle_selection)
        {
            const Color bg = detail::subtle_row_background(theme, state, sel);
            if (bg.a > 0.0f)
            {
                renderer.fill_rounded_rect(bounds().inset(BackplateMargin), theme.corner_radius, bg);
            }
            if (sel && !detail::controls(theme).row_selection_fills &&
                (mode == SelectionMode::Single || mode == SelectionMode::Extended))
            {
                const float h_item = bounds().height;
                float       h = h_item <= IndicatorMinHeight ? h_item : std::max(IndicatorMinHeight, h_item - 40.0f);
                if (state == ButtonState::Pressed)
                {
                    h = std::max(0.0f, h - IndicatorPressedShrink);
                }
                const Rect pill{bounds().x + IndicatorMargin, bounds().y + (h_item - h) * 0.5f, IndicatorWidth, h};
                renderer.fill_rounded_rect(pill,
                                           ListViewBaseIndicatorRadius,
                                           owner_.is_enabled() ? theme.colors().accent : theme.palette.accent_fill_disabled);
            }
        }
        else if (sel)
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, theme.colors().selection);
        }
        else if (state == ButtonState::Hover || state == ButtonState::Pressed)
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, theme.colors().control_fill_hover.with_alpha(0.10f));
        }
        if (mode == SelectionMode::Multiple)
        {
            const Rect box{bounds().x + CheckInlineLeft,
                           bounds().y + (bounds().height - detail::ItemCheckBoxSize) * 0.5f,
                           detail::ItemCheckBoxSize,
                           detail::ItemCheckBoxSize};
            detail::draw_item_check_box(renderer, theme, box, sel, state, /*on_image=*/false, ListViewBaseCheckGlyphSize);
        }
    }

    void render_grid_backplate(Renderer& renderer, const Theme& theme) const
    {
        if (!detail::controls(theme).list_item_subtle_selection)
        {
            return;
        }
        const bool        sel   = selected();
        const ButtonState state = this->state();
        // A selected tile is tertiary at rest and hovered, secondary pressed.
        Color bg = theme.palette.subtle_fill_transparent;
        if (sel)
        {
            bg = state == ButtonState::Pressed ? theme.palette.subtle_fill_secondary : theme.palette.subtle_fill_tertiary;
        }
        else if (state == ButtonState::Hover)
        {
            bg = theme.palette.subtle_fill_secondary;
        }
        else if (state == ButtonState::Pressed)
        {
            bg = theme.palette.subtle_fill_tertiary;
        }
        if (bg.a > 0.0f)
        {
            renderer.fill_rounded_rect(bounds(), theme.corner_radius, bg);
        }
    }

    void render_grid_overlay(Renderer& renderer, const Theme& theme) const
    {
        const bool        sel    = selected();
        const ButtonState state  = this->state();
        const bool        fluent = detail::controls(theme).list_item_subtle_selection;
        const float       radius = fluent ? theme.corner_radius : 0.0f;
        if (sel)
        {
            Color accent = fluent ? theme.colors().accent : theme.colors().selection;
            if (fluent && state == ButtonState::Hover)
            {
                accent = theme.colors().accent_hover;
            }
            else if (fluent && state == ButtonState::Pressed)
            {
                accent = theme.colors().accent_pressed;
            }
            else if (!owner_.is_enabled())
            {
                accent = fluent ? theme.palette.accent_fill_disabled : theme.colors().text_disabled;
            }
            // The 2px accent frame, and the 1px inner ring nested inside it (the accent is opaque).
            renderer.stroke_rounded_rect(bounds().inset(Thickness{GridSelectedBorder * 0.5f}),
                                         std::max(0.0f, radius - GridSelectedBorder * 0.5f),
                                         accent,
                                         GridSelectedBorder);
            if (fluent)
            {
                renderer.stroke_rounded_rect(bounds().inset(Thickness{GridSelectedBorder + 0.5f}),
                                             std::max(3.0f, radius - GridSelectedBorder),
                                             theme.palette.control_solid_fill_default,
                                             1.0f);
            }
        }
        else if (state == ButtonState::Hover && owner_.is_enabled())
        {
            const Color border = fluent ? theme.palette.control_stroke_on_accent_tertiary : theme.colors().selection;
            renderer.stroke_rounded_rect(bounds().inset(Thickness{0.5f}), std::max(0.0f, radius - 0.5f), border, 1.0f);
        }
        if (owner_.selection_mode_ == SelectionMode::Multiple)
        {
            const Rect box{bounds().right() - GridCheckMargin - detail::ItemCheckBoxSize,
                           bounds().y + GridCheckMargin,
                           detail::ItemCheckBoxSize,
                           detail::ItemCheckBoxSize};
            detail::draw_item_check_box(renderer, theme, box, sel, state, /*on_image=*/true, ListViewBaseCheckGlyphSize);
        }
    }

    // ---- plumbing ----
    void for_each_child(ChildVisitor visit) const override
    {
        if (content_)
        {
            visit(*content_);
        }
    }
    // Neither the row nor anything in its template is a tab stop: the list is the one stop and
    // moves its own cursor over the rows.
    void collect_focusables(std::vector<Element*>&) override
    {
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return role_ == Role::Item ? AutomationRole::ListItem : AutomationRole::Group;
    }
    // A templated item is named by its content: the template's own name, else the first name
    // inside it, which is usually the item's text.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        if (!content_)
        {
            return {};
        }
        const auto first_name = [](const auto& self, const Element& node) -> std::string
        {
            if (std::string name = node.automation_name(); !name.empty())
            {
                return name;
            }
            for (const Element* child : ui::automation_children(node))
            {
                if (std::string name = self(self, *child); !name.empty())
                {
                    return name;
                }
            }
            return {};
        };
        return first_name(first_name, *content_);
    }
    void automation_providers(AutomationProviders& out) override
    {
        if (role_ == Role::Item)
        {
            out.selection_item = this;
            // An item a click acts on: what a screen reader's "activate" does.
            if (owner_.is_item_click_enabled() || owner_.is_item_invoked_enabled())
            {
                out.invoke = this;
            }
        }
    }
    bool invoke() override
    {
        const std::optional<std::size_t> i = item();
        if (!i || !owner_.is_enabled())
        {
            return false;
        }
        owner_.invoke_from_automation(*i);
        return true;
    }
    [[nodiscard]] bool item_selected() const override
    {
        return selected();
    }
    bool select_item() override
    {
        if (const std::optional<std::size_t> i = item())
        {
            owner_.set_selected_index(*i);
            return true;
        }
        return false;
    }
    bool add_to_selection() override
    {
        const std::optional<std::size_t> i = item();
        if (!i || owner_.selection_mode() == SelectionMode::None)
        {
            return false;
        }
        owner_.select(*i); // alone in Single mode
        return true;
    }
    bool remove_from_selection() override
    {
        const std::optional<std::size_t> i = item();
        if (!i || owner_.selection_mode() == SelectionMode::None)
        {
            return false;
        }
        owner_.deselect(*i);
        return true;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        last_theme_       = &context.theme;
        const Thickness p = padding(context.theme);
        if (!content_)
        {
            return Size{p.horizontal(), p.vertical()};
        }
        const Size room{std::max(0.0f, available.width - p.horizontal()), std::max(0.0f, available.height - p.vertical())};
        const Size d     = content_->measure(room, context);
        content_desired_ = d;
        return Size{d.width + p.horizontal(), d.height + p.vertical()};
    }

    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        if (!content_ || last_theme_ == nullptr)
        {
            if (content_)
            {
                content_->arrange(final_rect);
            }
            return;
        }
        const Rect inner = final_rect.inset(padding(*last_theme_));
        // The item's content: stretched horizontally in a list row, centered in a tile, and
        // centered vertically (top for a header).
        const Alignment h   = role_ == Role::GroupHeader ? Alignment::Leading : owner_.content_h_align_;
        const float     w   = h == Alignment::Stretch ? inner.width : std::min(content_desired_.width, inner.width);
        const float     x   = h == Alignment::Center     ? inner.x + (inner.width - w) * 0.5f
                              : h == Alignment::Trailing ? inner.right() - w
                                                         : inner.x;
        const bool      top = role_ == Role::GroupHeader;
        const float     ch  = std::min(content_desired_.height, inner.height);
        const float     y   = top ? inner.y : inner.y + (inner.height - ch) * 0.5f;
        content_->arrange(Rect{x, y, w, ch});
    }

    // ---- render ----
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        last_theme_        = &theme;
        if (role_ == Role::GroupHeader)
        {
            if (content_)
            {
                content_->render(renderer, context);
            }
            // The 1px divider under the header text.
            const float y = bounds().bottom() - 0.5f;
            renderer.stroke_line(Point{bounds().x + ListViewBaseHeaderPadding.left, y},
                                 Point{bounds().right() - ListViewBaseHeaderPadding.right, y},
                                 theme.colors().divider,
                                 1.0f);
            return;
        }
        if (owner_.kind_ == ListViewBase::Kind::List)
        {
            render_list_chrome(renderer, theme);
            if (content_)
            {
                content_->render(renderer, context);
            }
            return;
        }
        render_grid_backplate(renderer, theme);
        if (content_)
        {
            content_->render(renderer, context);
        }
        render_grid_overlay(renderer, theme);
    }

    // ---- input ----
    bool pointer_override(const PointerEvent& e) override
    {
        if (role_ != Role::Item || !owner_.is_enabled())
        {
            return false;
        }
        const bool inside  = bounds().contains(e.position);
        bool       handled = false;
        switch (e.action)
        {
            case PointerAction::Move:
                if (hovered_ != inside)
                {
                    hovered_ = inside;
                    mark_dirty(DirtyFlags::Visual);
                }
                if (pressed_ && left_pressed_ &&
                    (std::abs(e.position.x - press_.x) > ListViewBaseDragThreshold ||
                     std::abs(e.position.y - press_.y) > ListViewBaseDragThreshold))
                {
                    if (const std::optional<std::size_t> i = item())
                    {
                        left_pressed_ = false; // one drag per press
                        owner_.start_item_drag(*i, e.position);
                    }
                }
                break;
            case PointerAction::Down:
                if (inside && (e.button == PointerButton::Left || e.button == PointerButton::Right))
                {
                    pressed_      = true;
                    hovered_      = true;
                    left_pressed_ = e.button == PointerButton::Left;
                    press_        = e.position;
                    clicks_       = e.click_count;
                    handled       = true;
                    mark_dirty(DirtyFlags::Visual);
                    if (const std::optional<std::size_t> i = item())
                    {
                        owner_.item_pressed(*i);
                    }
                }
                break;
            case PointerAction::Up:
            {
                // A drag this item started that no window took over ends here, with nothing done.
                if (DragDrop::pending())
                {
                    DragDrop::cancel();
                    owner_.drag_completed(DataPackageOperation::None);
                }
                // A click is a press and a release on the same item, as a row's pointer capture makes
                // it: a release this item never saw the press for is someone else's click.
                const bool clicked = pressed_ && left_pressed_ && e.button == PointerButton::Left && inside;
                handled            = pressed_;
                pressed_           = false;
                left_pressed_      = false;
                mark_dirty(DirtyFlags::Visual);
                if (clicked)
                {
                    if (const std::optional<std::size_t> i = item())
                    {
                        owner_.item_released(*i, e.modifiers.ctrl, e.modifiers.shift, clicks_);
                    }
                    return true; // the release may have rebuilt the rows, this one included
                }
                break;
            }
            case PointerAction::Leave:
                if (hovered_ || pressed_)
                {
                    hovered_      = false;
                    pressed_      = false;
                    left_pressed_ = false;
                    mark_dirty(DirtyFlags::Visual);
                }
                break;
            default:
                break;
        }
        return handled;
    }
    // A control in the row took the press: the row is not selected, but the keyboard carries on
    // from it.
    void handled_pointer_override(const PointerEvent& e) override
    {
        if (role_ == Role::Item && owner_.is_enabled() && e.action == PointerAction::Down && e.button == PointerButton::Left)
        {
            if (const std::optional<std::size_t> i = item())
            {
                owner_.set_current(*i);
            }
        }
    }

private:
    ListViewBase&            owner_;
    Role                     role_;
    std::unique_ptr<Element> content_;
    bool                     hovered_      = false;
    bool                     pressed_      = false;
    bool                     left_pressed_ = false;
    int                      clicks_       = 1; // the press's click_count: 2 for a double-click
    Point                    press_{};
    mutable Size             content_desired_{};
    mutable const Theme*     last_theme_ = nullptr;
};

namespace
{

// The default item template for a list of strings: the item's text, read live from the list so an
// insert or a move never leaves a row showing the wrong one.
class ListItemText final : public Element
{
public:
    explicit ListItemText(const ListViewBase& owner) : owner_(owner)
    {
    }

    [[nodiscard]] std::string text() const
    {
        const auto* container = dynamic_cast<const ListItemContainer*>(parent());
        if (container == nullptr)
        {
            return {};
        }
        const std::optional<std::size_t> item = container->item();
        return item && *item < owner_.items().size() ? owner_.items()[*item] : std::string{};
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text();
    }
    void set_foreground(std::optional<Color> color)
    {
        foreground_ = color;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext& context) const override
    {
        return context.measurer.measure(text(), leading_text(context.theme));
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme     = context.theme;
        const auto*  container = dynamic_cast<const ListItemContainer*>(parent());
        Color        color     = foreground_ ? *foreground_ : theme.colors().text;
        if (!owner_.is_enabled())
        {
            color = theme.colors().text_disabled;
        }
        else if (!foreground_ && !detail::controls(theme).list_item_subtle_selection && container != nullptr &&
                 container->selected())
        {
            color = theme.colors().selection_text; // an era that fills a selected row
        }
        renderer.draw_text(text(), bounds(), color, leading_text(theme));
    }


private:
    const ListViewBase&  owner_;
    std::optional<Color> foreground_;
};

class GroupHeaderText final : public Element
{
public:
    explicit GroupHeaderText(std::string text) : text_(std::move(text))
    {
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text_;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext& context) const override
    {
        return context.measurer.measure(text_, style(context.theme));
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        renderer.draw_text(text_, bounds(), theme.colors().text, style(theme));
    }


private:
    static TextStyle style(const Theme& theme)
    {
        TextStyle s = leading_text(theme);
        s.font_size = ListViewBaseHeaderFontSize;
        return s;
    }
    std::string text_;
};

} // namespace

// ---- ListViewBase ----

ListViewBase::ListViewBase(Kind kind) :
    kind_(kind),
    selection_(std::make_unique<detail::ItemSelection>()),
    content_h_align_(kind == Kind::List ? Alignment::Stretch : Alignment::Center)
{
    scroll_owner_ = std::make_unique<ScrollControl>();
    scroll_       = scroll_owner_.get();
    set_parent_of(*scroll_, this);
    content_  = scroll_->set_child(std::make_unique<StackControl>(Orientation::Vertical));
    repeater_ = content_->add(std::make_unique<ForEachControl>());
    repeater_->set_item_template([this](std::size_t flat) { return make_container(flat); });
    if (kind == Kind::Grid)
    {
        container_margin_ = GridContainerMargin;
        padding_          = Edges{.bottom = 10.0f}; // a TileView's padding
        content_->set_margin(padding_);
    }
}

ListViewBase::~ListViewBase() = default;

ListViewControl::ListViewControl() : ListViewBase(Kind::List)
{
    repeater()->set_layout(std::make_unique<StackLayoutEngine>());
    set_min_width(ListItemMinWidth);
}

TileViewControl::TileViewControl() : ListViewBase(Kind::Grid)
{
    // A tile grid: cells the first item's size, as many across as fit, left-aligned.
    grid_ = repeater()->set_layout(std::make_unique<TileLayoutEngine>());
}

void TileViewControl::replace_layout(std::unique_ptr<Layout> layout)
{
    if (!layout)
    {
        layout = std::make_unique<TileLayoutEngine>();
    }
    grid_ = dynamic_cast<TileLayoutEngine*>(layout.get());
    repeater()->set_layout(std::move(layout));
    if (grid_ != nullptr)
    {
        grid_->set_orientation(orientation_);
    }
    push_group_sizes();
}

std::size_t TileViewControl::maximum_rows_or_columns() const
{
    return grid_ != nullptr ? grid_->maximum_rows_or_columns() : 0;
}

void TileViewControl::set_maximum_rows_or_columns(std::size_t max)
{
    if (grid_ != nullptr)
    {
        grid_->set_maximum_rows_or_columns(max);
    }
}

void TileViewControl::set_orientation(Orientation orientation)
{
    orientation_ = orientation;
    if (grid_ != nullptr)
    {
        grid_->set_orientation(orientation == Orientation::Horizontal ? Orientation::Horizontal : Orientation::Vertical);
    }
    scroll_view()->set_content_orientation(
        orientation == Orientation::Horizontal ? ScrollContentOrientation::Vertical : ScrollContentOrientation::Horizontal);
}

void TileViewControl::set_item_width(OptionalFloat width)
{
    if (grid_ != nullptr)
    {
        grid_->set_min_item_width(width);
    }
}

void TileViewControl::set_item_height_cell(OptionalFloat height)
{
    if (grid_ != nullptr)
    {
        grid_->set_min_item_height(height);
    }
}

// ---- flat index space ----

std::size_t ListViewBase::flat_count() const
{
    return grouped() ? count_ + group_names_.size() : count_;
}

std::size_t ListViewBase::group_of_rank(std::size_t rank) const
{
    const auto it = std::upper_bound(group_start_.begin(), group_start_.end(), rank);
    return it == group_start_.begin() ? 0 : static_cast<std::size_t>(it - group_start_.begin()) - 1;
}

std::optional<std::size_t> ListViewBase::flat_to_item(std::size_t flat) const
{
    if (!grouped())
    {
        return flat < count_ ? std::optional<std::size_t>{flat} : std::nullopt;
    }
    const auto it = std::upper_bound(group_flat_.begin(), group_flat_.end(), flat);
    if (it == group_flat_.begin())
    {
        return std::nullopt;
    }
    const auto g = static_cast<std::size_t>(it - group_flat_.begin()) - 1;
    if (flat == group_flat_[g])
    {
        return std::nullopt; // the header
    }
    const std::size_t k = flat - group_flat_[g] - 1;
    return k < group_sizes_[g] ? std::optional<std::size_t>{order_[group_start_[g] + k]} : std::nullopt;
}

std::optional<std::size_t> ListViewBase::flat_to_group(std::size_t flat) const
{
    if (!grouped())
    {
        return std::nullopt;
    }
    const auto it = std::lower_bound(group_flat_.begin(), group_flat_.end(), flat);
    return it != group_flat_.end() && *it == flat
               ? std::optional<std::size_t>{static_cast<std::size_t>(it - group_flat_.begin())}
               : std::nullopt;
}

std::size_t ListViewBase::item_to_flat(std::size_t item) const
{
    if (!grouped() || item >= rank_.size())
    {
        return item;
    }
    const std::size_t rank = rank_[item];
    return rank + group_of_rank(rank) + 1; // the headers before it and its own
}

void ListViewBase::regroup()
{
    group_names_.clear();
    group_sizes_.clear();
    group_start_.clear();
    group_flat_.clear();
    order_.clear();
    rank_.clear();
    if (group_keys_.empty())
    {
        return;
    }
    // Groups in the order their keys first appear; items keep their order within a group.
    static const std::string                     NoKey;
    std::unordered_map<std::string, std::size_t> index;
    std::vector<std::size_t>                     group_of(count_);
    for (std::size_t i = 0; i < count_; ++i)
    {
        const std::string& key    = i < group_keys_.size() ? group_keys_[i] : NoKey;
        const auto [it, inserted] = index.try_emplace(key, group_names_.size());
        if (inserted)
        {
            group_names_.push_back(key);
            group_sizes_.push_back(0);
        }
        group_of[i] = it->second;
        ++group_sizes_[it->second];
    }
    std::size_t start = 0;
    for (std::size_t g = 0; g < group_names_.size(); ++g)
    {
        group_start_.push_back(start);
        group_flat_.push_back(start + g);
        start += group_sizes_[g];
    }
    std::vector<std::size_t> next = group_start_;
    order_.resize(count_);
    rank_.resize(count_);
    for (std::size_t i = 0; i < count_; ++i)
    {
        const std::size_t rank = next[group_of[i]]++;
        order_[rank]           = i;
        rank_[i]               = rank;
    }
}

const std::string& ListViewBase::group_key(std::size_t group) const
{
    static const std::string None;
    return group < group_names_.size() ? group_names_[group] : None;
}

bool ListViewBase::set_span(detail::ItemSelection& s, std::size_t a, std::size_t b, bool selected)
{
    if (!grouped())
    {
        return s.set_range(a, b, selected);
    }
    const std::size_t from    = std::min(rank_of(a), rank_of(b));
    const std::size_t to      = std::max(rank_of(a), rank_of(b));
    bool              changed = false;
    for (std::size_t rank = from; rank <= to && rank < count_; ++rank)
    {
        changed = s.set(item_at_rank(rank), selected) || changed;
    }
    return changed;
}

void ListViewBase::push_group_sizes()
{
    if (auto* tiles = dynamic_cast<TileLayoutEngine*>(repeater_->layout()))
    {
        tiles->set_group_sizes(grouped() ? group_sizes_ : std::vector<std::size_t>{});
    }
}

Element* ListViewBase::automation_focus() const
{
    return current_ ? container_for_item(*current_) : nullptr;
}

ListItemContainer* ListViewBase::container_for_item(std::size_t item) const
{
    if (item >= count_)
    {
        return nullptr;
    }
    return dynamic_cast<ListItemContainer*>(repeater_->try_get_element(item_to_flat(item)));
}

std::unique_ptr<Element> ListViewBase::make_container(std::size_t flat)
{
    if (const std::optional<std::size_t> group = flat_to_group(flat))
    {
        std::unique_ptr<Element> content = group_header_template_ ? group_header_template_(*group) : nullptr;
        if (!content)
        {
            content = std::make_unique<GroupHeaderText>(group_key(*group));
        }
        return std::make_unique<ListItemContainer>(*this, ListItemContainer::Role::GroupHeader, std::move(content));
    }
    const std::optional<std::size_t> item    = flat_to_item(flat);
    std::unique_ptr<Element>         content = item && item_template_ ? item_template_(*item) : nullptr;
    if (!content && !item_template_)
    {
        auto text = std::make_unique<ListItemText>(*this);
        if (last_theme_ != nullptr)
        {
            text->set_foreground(foreground_.resolve(*last_theme_));
        }
        content = std::move(text);
    }
    return std::make_unique<ListItemContainer>(*this, ListItemContainer::Role::Item, std::move(content));
}

void ListViewBase::reset_repeater()
{
    regroup();
    push_group_sizes();
    repeater_->set_item_count(flat_count());
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- items ----

void ListViewBase::set_count(std::size_t count)
{
    count_ = count;
    selection_->reset(count);
    current_.reset();
    focus_index_ = detail::NoIndex;
    ids_.resize(count);
    for (std::uint64_t& id : ids_)
    {
        id = next_id_++;
    }
    if (updating_mode_ == ItemsUpdatingScrollMode::KeepLastItemInView)
    {
        keep_last_pending_ = true; // a new collection opens at its end
    }
    reset_repeater();
}

void ListViewBase::set_items(std::vector<std::string> items)
{
    open_batch(true);
    const bool had_selection = selection_->selected_count() > 0;
    items_                   = std::move(items);
    set_count(items_.size());
    if (had_selection && on_selection_changed_)
    {
        on_selection_changed_(selected_index());
    }
}

void ListViewBase::set_item_template(ItemTemplate item_template)
{
    item_template_ = std::move(item_template);
    reset_repeater();
}

void ListViewBase::set_item_count(std::size_t count)
{
    open_batch(true);
    items_.clear();
    const bool had_selection = selection_->selected_count() > 0;
    set_count(count);
    if (had_selection && on_selection_changed_)
    {
        on_selection_changed_(selected_index());
    }
}

void ListViewBase::notify_inserted(std::size_t index, std::size_t count)
{
    if (count == 0)
    {
        return;
    }
    index = std::min(index, count_);
    open_batch(false);
    const std::optional<std::size_t> was_selected = selected_index();
    const bool                       at_end       = scroll_->vertical_offset() >= scroll_->scrollable_height() - 0.5f;
    count_ += count;
    ids_.insert(ids_.begin() + static_cast<std::ptrdiff_t>(index), count, 0);
    for (std::size_t i = index; i < index + count; ++i)
    {
        ids_[i] = next_id_++;
    }
    selection_->on_inserted(index, count);
    if (current_ && *current_ >= index)
    {
        *current_ += count;
    }
    if (!grouped())
    {
        repeater_->notify_inserted(index, count);
    }
    else
    {
        reset_repeater();
    }
    if (updating_mode_ == ItemsUpdatingScrollMode::KeepLastItemInView && at_end)
    {
        keep_last_pending_ = true;
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    report_index_shift(was_selected); // the selected item moved down
}

void ListViewBase::insert_item(std::size_t index, std::string item)
{
    index = std::min(index, items_.size());
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
    notify_inserted(index, 1);
}

void ListViewBase::notify_removed(std::size_t index, std::size_t count)
{
    if (index >= count_ || count == 0)
    {
        return;
    }
    count = std::min(count, count_ - index);
    open_batch(false);
    const std::optional<std::size_t> was_selected = selected_index();
    for (std::size_t i = index; i < index + count; ++i)
    {
        add_ghost(i);
    }
    count_ -= count;
    ids_.erase(ids_.begin() + static_cast<std::ptrdiff_t>(index),
               ids_.begin() + static_cast<std::ptrdiff_t>(index + count));
    const bool changed = selection_->on_removed(index, count);
    if (current_)
    {
        if (*current_ >= index + count)
        {
            *current_ -= count;
        }
        else if (*current_ >= index)
        {
            current_ = count_ == 0 ? std::nullopt : std::optional<std::size_t>{std::min(index, count_ - 1)};
        }
    }
    if (focus_index_ != detail::NoIndex && focus_index_ >= count_)
    {
        focus_index_ = count_ == 0 ? detail::NoIndex : count_ - 1;
    }
    if (!grouped())
    {
        repeater_->notify_removed(index, count);
    }
    else
    {
        reset_repeater();
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (changed && on_selection_changed_)
    {
        on_selection_changed_(selected_index());
    }
    else if (!changed)
    {
        report_index_shift(was_selected); // a removal before the selected item
    }
}

void ListViewBase::remove_item(std::size_t index)
{
    if (index >= items_.size())
    {
        return;
    }
    // The ghost is recorded before the text goes.
    notify_removed(index, 1);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
}

std::optional<std::size_t> ListViewBase::index_from_element(const Element& inside) const
{
    for (const Element* e = &inside; e != nullptr; e = e->parent())
    {
        if (const auto* c = dynamic_cast<const ListItemContainer*>(e); c != nullptr && e->parent() == repeater_)
        {
            return c->item();
        }
    }
    return std::nullopt;
}

void ListViewBase::notify_replaced(std::size_t index, std::size_t count)
{
    if (index >= count_)
    {
        return;
    }
    count = std::min(count, count_ - index);
    if (!grouped())
    {
        repeater_->notify_replaced(index, count);
    }
    else
    {
        reset_repeater();
    }
}

void ListViewBase::notify_moved(std::size_t from, std::size_t to)
{
    if (from >= count_ || to >= count_ || from == to)
    {
        return;
    }
    open_batch(false);
    const std::optional<std::size_t> was_selected = selected_index();
    const std::uint64_t              id           = ids_[from];
    ids_.erase(ids_.begin() + static_cast<std::ptrdiff_t>(from));
    ids_.insert(ids_.begin() + static_cast<std::ptrdiff_t>(to), id);
    selection_->on_moved(from, to);
    if (current_ && *current_ == from)
    {
        current_ = to;
    }
    if (!grouped())
    {
        repeater_->notify_moved(from, to);
    }
    else
    {
        reset_repeater();
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    report_index_shift(was_selected);
}

void ListViewBase::report_index_shift(std::optional<std::size_t> was_selected)
{
    if (selected_index() != was_selected && on_selected_index_shifted_)
    {
        on_selected_index_shifted_(selected_index());
    }
}

void ListViewBase::move_item(std::size_t from, std::size_t to)
{
    if (from >= items_.size() || to >= items_.size() || from == to)
    {
        return;
    }
    std::string moved = std::move(items_[from]);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(from));
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    notify_moved(from, to);
}

void ListViewBase::notify_reset()
{
    set_item_count(count_);
}

// ---- groups / header / footer ----

void ListViewBase::set_group_keys(std::vector<std::string> keys)
{
    if (keys == group_keys_)
    {
        return;
    }
    group_keys_ = std::move(keys);
    sticky_.reset();
    sticky_group_.reset();
    reset_repeater();
}

void ListViewBase::set_group_header_template(GroupHeaderTemplate header_template)
{
    group_header_template_ = std::move(header_template);
    sticky_.reset();
    sticky_group_.reset();
    reset_repeater();
}

void ListViewBase::set_sticky_group_headers(bool sticky)
{
    sticky_headers_ = sticky;
    if (!sticky)
    {
        sticky_.reset();
        sticky_group_.reset();
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ListViewBase::set_header_text(std::string text)
{
    const bool had_text = !header_text_.empty();
    header_text_        = std::move(text);
    if (header_text_.empty())
    {
        if (had_text)
        {
            replace_header(nullptr, true);
        }
        return;
    }
    auto label = std::make_unique<GroupHeaderText>(header_text_);
    label->set_margin(Edges{.left   = ListViewBaseHeaderPadding.left,
                            .top    = ListViewBaseHeaderPadding.top,
                            .right  = ListViewBaseHeaderPadding.right,
                            .bottom = ListViewBaseHeaderMargin.bottom});
    replace_header(std::move(label), true);
}

void ListViewBase::replace_header(std::unique_ptr<Element> element, bool header)
{
    Element*& slot = header ? header_ : footer_;
    if (slot != nullptr)
    {
        (void)content_->remove(slot);
        slot = nullptr;
    }
    if (element)
    {
        slot = header ? content_->insert(0, std::move(element)) : content_->add(std::move(element));
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- panel, containers and appearance ----

void ListViewBase::set_panel_vertical_alignment(Alignment alignment)
{
    content_->set_vertical_alignment(alignment);
}

void ListViewBase::set_item_container_margin(const Thickness& margin)
{
    container_margin_ = margin;
    for (std::size_t flat : repeater_->realized_indices())
    {
        if (auto* c = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat)))
        {
            c->apply_style();
        }
    }
    reset_repeater(); // a TileView's cells are sized from the first container, margin included
}

void ListViewBase::set_item_horizontal_content_alignment(Alignment alignment)
{
    content_h_align_ = alignment;
    invalidate_containers();
}

void ListViewBase::invalidate_containers()
{
    for (std::size_t flat : repeater_->realized_indices())
    {
        if (auto* c = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat)))
        {
            c->invalidate();
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ListViewBase::set_item_height(OptionalFloat height)
{
    item_height_ = height ? std::optional<float>{std::max(0.0f, *height)} : std::nullopt;
    reset_repeater();
}

void ListViewBase::set_background(std::optional<Color> color)
{
    background_.set(color);
    mark_dirty(DirtyFlags::Visual);
}

void ListViewBase::set_background(ThemeColor resource)
{
    background_.set(resource);
    mark_dirty(DirtyFlags::Visual);
}

void ListViewBase::set_foreground(std::optional<Color> color)
{
    foreground_.set(color);
    reset_repeater();
}

void ListViewBase::set_foreground(ThemeColor resource)
{
    foreground_.set(resource);
    reset_repeater();
}

void ListViewBase::set_border_brush(std::optional<Color> color)
{
    border_brush_.set(color);
    mark_dirty(DirtyFlags::Visual);
}

void ListViewBase::set_border_brush(ThemeColor resource)
{
    border_brush_.set(resource);
    mark_dirty(DirtyFlags::Visual);
}

void ListViewBase::set_border_thickness(const Thickness& thickness)
{
    border_thickness_ = thickness;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ListViewBase::set_padding(const Thickness& padding)
{
    padding_ = padding;
    content_->set_margin(padding);
}

void ListViewBase::enabled_changed()
{
    mark_dirty(DirtyFlags::Visual);
}

// ---- selection ----

void ListViewBase::set_selection_mode(SelectionMode mode)
{
    if (selection_mode_ == mode)
    {
        return;
    }
    selection_mode_ = mode;
    bool changed    = false;
    if (mode == SelectionMode::None)
    {
        changed = selection_->clear();
    }
    else if (mode == SelectionMode::Single && selection_->selected_count() > 1)
    {
        const std::optional<std::size_t> keep = selection_->first_selected();
        selection_->clear();
        (void)selection_->set(*keep, true);
        changed = true;
    }
    // The content moves over for the check boxes, so the rows are laid out again.
    invalidate_containers();
    if (changed)
    {
        selection_changed();
    }
}

std::optional<std::size_t> ListViewBase::selected_index() const
{
    return selection_->first_selected();
}

std::vector<std::size_t> ListViewBase::selected_items() const
{
    return selection_->selected();
}

bool ListViewBase::is_selected(std::size_t index) const
{
    return selection_->is_selected(index);
}

void ListViewBase::selection_changed()
{
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected_index());
    }
}

void ListViewBase::set_selected_index(std::optional<std::size_t> requested)
{
    if (!requested)
    {
        if (selection_->clear())
        {
            selection_changed();
        }
        return;
    }
    if (count_ == 0 || selection_mode_ == SelectionMode::None)
    {
        return;
    }
    const std::size_t index = std::min(*requested, count_ - 1);
    if (selection_->selected_count() == 1 && selection_->is_selected(index))
    {
        scroll_into_view(index);
        return;
    }
    (void)selection_->clear();
    (void)selection_->set(index, true);
    selection_->set_anchor(index);
    scroll_into_view(index);
    selection_changed();
}

void ListViewBase::set_selected_items(std::vector<std::size_t> indices)
{
    std::erase_if(indices, [this](std::size_t index) { return index >= count_; });
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (selection_mode_ == SelectionMode::None)
    {
        indices.clear();
    }
    else if (selection_mode_ == SelectionMode::Single && indices.size() > 1)
    {
        indices.resize(1);
    }
    if (indices == selected_items())
    {
        return;
    }
    (void)selection_->clear();
    for (const std::size_t index : indices)
    {
        (void)selection_->set(index, true);
    }
    if (!indices.empty())
    {
        selection_->set_anchor(indices.front());
    }
    selection_changed();
}

void ListViewBase::select(std::size_t index)
{
    if (selection_mode_ == SelectionMode::None || index >= count_)
    {
        return;
    }
    bool changed = false;
    if (selection_mode_ == SelectionMode::Single)
    {
        changed = selection_->clear();
    }
    changed = selection_->set(index, true) || changed;
    if (changed)
    {
        selection_changed();
    }
}

void ListViewBase::deselect(std::size_t index)
{
    if (selection_->set(index, false))
    {
        selection_changed();
    }
}

void ListViewBase::select_all()
{
    if ((selection_mode_ == SelectionMode::Multiple || selection_mode_ == SelectionMode::Extended) &&
        selection_->select_all())
    {
        selection_changed();
    }
}

void ListViewBase::deselect_all()
{
    if (selection_->clear())
    {
        selection_changed();
    }
}

// The primary gesture: a click, Enter, Space.
void ListViewBase::primary_gesture(std::size_t item, bool shift)
{
    detail::ItemSelection& s       = *selection_;
    bool                   changed = false;
    const std::size_t      anchor  = s.anchor() ? *s.anchor() : selected_index().value_or(0);
    switch (selection_mode_)
    {
        case SelectionMode::None:
            return;
        case SelectionMode::Single:
            if (!s.is_selected(item) || s.selected_count() != 1)
            {
                changed = s.clear();
                changed = s.set(item, true) || changed;
            }
            s.set_anchor(item);
            break;
        case SelectionMode::Multiple:
            if (shift)
            {
                changed = set_span(s, anchor, item, s.is_selected(anchor));
            }
            else
            {
                changed = s.set(item, !s.is_selected(item));
                s.set_anchor(item);
            }
            break;
        case SelectionMode::Extended:
            if (shift)
            {
                changed = s.clear();
                changed = set_span(s, anchor, item, true) || changed;
                s.set_anchor(anchor);
            }
            else
            {
                if (!(s.selected_count() == 1 && s.is_selected(item)))
                {
                    changed = s.clear();
                    changed = s.set(item, true) || changed;
                }
                s.set_anchor(item);
            }
            break;
    }
    if (changed)
    {
        selection_changed();
    }
}

// The secondary gesture: Ctrl+click, Ctrl+Space.
void ListViewBase::secondary_gesture(std::size_t item, bool shift)
{
    detail::ItemSelection& s       = *selection_;
    bool                   changed = false;
    switch (selection_mode_)
    {
        case SelectionMode::None:
            return;
        case SelectionMode::Single:
        {
            const bool was = s.is_selected(item);
            changed        = s.clear();
            changed        = s.set(item, !was) || changed;
            s.set_anchor(item);
            break;
        }
        case SelectionMode::Multiple:
        case SelectionMode::Extended:
            if (shift && s.anchor())
            {
                changed = set_span(s, *s.anchor(), item, true);
            }
            else
            {
                changed = s.set(item, !s.is_selected(item));
                s.set_anchor(item);
            }
            break;
    }
    if (changed)
    {
        selection_changed();
    }
}

void ListViewBase::keyboard_moved(std::size_t item, bool ctrl, bool shift)
{
    set_current(item);
    scroll_into_view(item);
    detail::ItemSelection& s       = *selection_;
    bool                   changed = false;
    switch (selection_mode_)
    {
        case SelectionMode::None:
            return;
        case SelectionMode::Single:
            if (!ctrl && selection_follows_focus_ && !(s.selected_count() == 1 && s.is_selected(item)))
            {
                changed = s.clear();
                changed = s.set(item, true) || changed;
                s.set_anchor(item);
            }
            break;
        case SelectionMode::Multiple:
            if (shift && s.anchor())
            {
                changed = set_span(s, *s.anchor(), item, true);
            }
            else if (!shift)
            {
                s.set_anchor(item);
            }
            break;
        case SelectionMode::Extended:
            if (shift)
            {
                const std::size_t anchor = s.anchor() ? *s.anchor() : item;
                if (!ctrl)
                {
                    changed = s.clear();
                }
                changed = set_span(s, anchor, item, true) || changed;
                s.set_anchor(anchor);
            }
            else if (!ctrl && !(s.selected_count() == 1 && s.is_selected(item)))
            {
                changed = s.clear();
                changed = s.set(item, true) || changed;
                s.set_anchor(item);
            }
            break;
    }
    if (changed)
    {
        selection_changed();
    }
}

void ListViewBase::item_pressed(std::size_t item)
{
    press_item_ = item;
}

void ListViewBase::invoke_item(std::size_t item)
{
    if (item_invoked_enabled_ && on_item_invoked_ && item < count_)
    {
        on_item_invoked_(item);
    }
}

void ListViewBase::invoke_from_automation(std::size_t item)
{
    set_current(item);
    // As Enter does: on_item_click, then on_item_invoked, unless the click handler removed the item.
    if (item_click_enabled_ && on_item_click_)
    {
        const std::uint64_t id = item < ids_.size() ? ids_[item] : 0;
        on_item_click_(item);
        if (item >= ids_.size() || ids_[item] != id)
        {
            return;
        }
    }
    invoke_item(item);
}

void ListViewBase::item_released(std::size_t item, bool ctrl, bool shift, int click_count)
{
    press_item_.reset();
    if (!is_enabled())
    {
        return;
    }
    set_current(item);
    // on_item_click first, then the selection as usual.
    if (item_click_enabled_ && on_item_click_)
    {
        // The handler may remove the item it was called for; the gesture is then not for the
        // neighbor that moved into its place.
        const std::uint64_t id = item < ids_.size() ? ids_[item] : 0;
        on_item_click_(item);
        if (item >= ids_.size() || ids_[item] != id)
        {
            return;
        }
    }
    if (ctrl)
    {
        secondary_gesture(item, shift);
    }
    else
    {
        primary_gesture(item, shift);
    }
    // on_item_invoked: a double-click, or any click when nothing can be selected.
    if (click_count >= 2 || selection_mode_ == SelectionMode::None)
    {
        invoke_item(item);
    }
}

// ---- navigation ----

void ListViewBase::set_current(std::size_t item)
{
    if (item < count_)
    {
        current_ = item;
        mark_dirty(DirtyFlags::Visual);
    }
}

void ListViewBase::set_current_item(std::size_t index)
{
    if (index >= count_)
    {
        return;
    }
    set_current(index);
    scroll_into_view(index);
}

std::optional<std::size_t> ListViewBase::neighbor(std::size_t from, Key key)
{
    if (count_ == 0)
    {
        return std::nullopt;
    }
    // Steps run in display order, which the groups may make different from item order.
    const std::size_t at    = rank_of(from);
    const auto        shown = [this](std::size_t rank) { return std::optional<std::size_t>{item_at_rank(rank)}; };
    switch (key)
    {
        case Key::Home:
            return shown(0);
        case Key::End:
            return shown(count_ - 1);
        default:
            break;
    }
    const bool forward = key == Key::ArrowDown || key == Key::ArrowRight || key == Key::PageDown;
    if (kind_ == Kind::List || key == Key::ArrowLeft || key == Key::ArrowRight)
    {
        if (key == Key::PageUp || key == Key::PageDown)
        {
            Element*    e    = repeater_->get_or_create_element(item_to_flat(from));
            const float h    = e != nullptr && e->bounds().height > 0.0f ? e->bounds().height : ListItemMinHeight;
            const auto  page = std::max<std::size_t>(1, static_cast<std::size_t>(scroll_->bounds().height / h) - 1);
            return shown(forward ? std::min(count_ - 1, at + page) : (at > page ? at - page : 0));
        }
        if (kind_ == Kind::List && (key == Key::ArrowLeft || key == Key::ArrowRight))
        {
            return std::nullopt; // a vertical list leaves Left/Right to move focus out
        }
        if (forward)
        {
            return at + 1 < count_ ? shown(at + 1) : std::nullopt;
        }
        return at > 0 ? shown(at - 1) : std::nullopt;
    }
    const auto* grid = dynamic_cast<const TileLayoutEngine*>(repeater_->layout());
    if ((grid == nullptr || grouped()) && (key == Key::ArrowUp || key == Key::ArrowDown))
    {
        // A layout whose items are not equal cells, or cells broken into groups: the nearest item
        // on a line above or below, by where the realized items are.
        const ListItemContainer* here = container_for_item(from);
        if (here == nullptr)
        {
            return forward ? (at + 1 < count_ ? shown(at + 1) : std::nullopt) : (at > 0 ? shown(at - 1) : std::nullopt);
        }
        const Rect                 origin = here->bounds();
        std::optional<std::size_t> best;
        float                      best_line = std::numeric_limits<float>::max();
        float                      best_side = std::numeric_limits<float>::max();
        for (std::size_t flat : repeater_->realized_indices())
        {
            const auto* c = dynamic_cast<const ListItemContainer*>(repeater_->try_get_element(flat));
            const std::optional<std::size_t> item = flat_to_item(flat);
            if (c == nullptr || !item || *item == from)
            {
                continue;
            }
            const Rect b = c->bounds();
            // On another line in the wanted direction: no vertical overlap with this one.
            const float line = forward ? b.y - origin.bottom() : origin.y - b.bottom();
            if (line < -0.5f)
            {
                continue;
            }
            const float side = std::abs(b.center().x - origin.center().x);
            if (line < best_line - 0.5f || (std::abs(line - best_line) <= 0.5f && side < best_side))
            {
                best_line = line;
                best_side = side;
                best      = *item;
            }
        }
        return best;
    }
    // A grid: Up/Down (and a page) to the item in the same column of the line above or below.
    const float cell  = grid != nullptr ? grid->effective_item_size().width : 0.0f;
    const float width = repeater_->bounds().width;
    const auto per_line = cell > 0.0f ? std::max<std::size_t>(1, static_cast<std::size_t>(width / cell)) : std::size_t{1};
    std::size_t step = per_line;
    if (grid != nullptr && grid->maximum_rows_or_columns() > 0)
    {
        step = std::min(step, grid->maximum_rows_or_columns());
    }
    if (key == Key::PageUp || key == Key::PageDown)
    {
        const float row_h = grid != nullptr ? grid->effective_item_size().height : GridItemMin;
        step *= std::max<std::size_t>(1, static_cast<std::size_t>(scroll_->bounds().height / std::max(1.0f, row_h)));
    }
    if (forward)
    {
        return at + step < count_ ? shown(at + step) : std::nullopt;
    }
    return at >= step ? shown(at - step) : std::nullopt;
}

void ListViewBase::scroll_into_view(std::size_t index, ScrollIntoViewAlignment alignment)
{
    if (index >= count_)
    {
        return;
    }
    Element* element = repeater_->get_or_create_element(item_to_flat(index));
    if (element == nullptr)
    {
        return;
    }
    // Only the list's own scroller moves, and a pinned group header
    // is not allowed to cover the item.
    Rect area = element->bounds();
    if (sticky_headers_ && grouped() && sticky_h_ > 0.0f)
    {
        area.y -= sticky_h_;
        area.height += sticky_h_;
    }
    BringIntoViewOptions options;
    if (alignment == ScrollIntoViewAlignment::Leading)
    {
        options.vertical_alignment_ratio   = 0.0f;
        options.horizontal_alignment_ratio = 0.0f;
    }
    (void)scroll_->reveal_child(area, options);
}

float ListViewBase::scroll_offset() const
{
    if (const auto* grid = dynamic_cast<const TileViewControl*>(this);
        grid != nullptr && grid->orientation() == Orientation::Vertical)
    {
        return scroll_->horizontal_offset();
    }
    return scroll_->vertical_offset();
}

std::string ListViewBase::relative_scroll_position(Callback<std::string(std::size_t)> key_of) const
{
    const float top = scroll_->bounds().y;
    for (std::size_t flat : repeater_->realized_indices())
    {
        const Element*                   e    = repeater_->try_get_element(flat);
        const std::optional<std::size_t> item = flat_to_item(flat);
        if (e == nullptr || !item || e->bounds().bottom() <= top)
        {
            continue;
        }
        return key_of(*item) + "|" + std::to_string(top - e->bounds().y);
    }
    return {};
}

bool ListViewBase::set_relative_scroll_position(const std::string&                                       position,
                                                Callback<std::optional<std::size_t>(const std::string&)> index_of)
{
    const std::size_t bar = position.rfind('|');
    if (bar == std::string::npos)
    {
        return false;
    }
    const std::optional<std::size_t> found = index_of(position.substr(0, bar));
    if (!found || *found >= count_)
    {
        return false;
    }
    const std::size_t item   = *found;
    float             offset = 0.0f;
    try
    {
        offset = std::stof(position.substr(bar + 1));
    } catch (...)
    {
        offset = 0.0f;
    }
    Element* element = repeater_->get_or_create_element(item_to_flat(item));
    if (element == nullptr)
    {
        return false;
    }
    const float target = scroll_->vertical_offset() + (element->bounds().y - scroll_->bounds().y) + offset;
    // At once, with no view-change request: the scroll is part of this call.
    scroll_->jump_to_vertical_offset(target);
    return true;
}

// ---- drag and drop ----

std::size_t ListViewBase::insertion_index_at(Point position) const
{
    if (kind_ != Kind::List)
    {
        return tile_insertion_index_at(position);
    }
    // Past the middle of the item under the point is after it (the insertion panels' rule).
    std::optional<std::size_t> best;
    float                      best_distance = std::numeric_limits<float>::max();
    for (std::size_t flat : repeater_->realized_indices())
    {
        const auto*                      c = dynamic_cast<const ListItemContainer*>(repeater_->try_get_element(flat));
        const std::optional<std::size_t> item = flat_to_item(flat);
        if (c == nullptr || !item)
        {
            continue;
        }
        const Rect b = c->bounds();
        if (b.contains(position))
        {
            const bool after = kind_ == Kind::List ? position.y > b.center().y : position.x > b.center().x;
            return after ? *item + 1 : *item;
        }
        const float d = std::hypot(b.center().x - position.x, b.center().y - position.y);
        if (d < best_distance)
        {
            best_distance    = d;
            const bool after = kind_ == Kind::List ? position.y > b.center().y
                                                   : (position.y > b.bottom() || position.x > b.center().x);
            best             = after ? *item + 1 : *item;
        }
    }
    return best ? std::min(*best, count_) : count_;
}

std::size_t ListViewBase::tile_insertion_index_at(Point position) const
{
    // The line first, then along it: the nearest item alone would put a point below the last line
    // after the item above it, in the middle of that line.
    std::vector<std::pair<std::size_t, Rect>> cells;
    for (std::size_t flat : repeater_->realized_indices())
    {
        const auto*                      c = dynamic_cast<const ListItemContainer*>(repeater_->try_get_element(flat));
        const std::optional<std::size_t> item = flat_to_item(flat);
        if (c != nullptr && item)
        {
            cells.emplace_back(*item, c->bounds());
        }
    }
    if (cells.empty())
    {
        return count_;
    }
    std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // Below the line of the last item is the end.
    if (cells.back().first + 1 == count_ && position.y > cells.back().second.bottom())
    {
        return count_;
    }
    // The line level with the point, or else the nearest one.
    float line_y   = cells.front().second.y;
    float distance = std::numeric_limits<float>::max();
    for (const auto& [item, b] : cells)
    {
        const float d = position.y < b.y ? b.y - position.y : (position.y > b.bottom() ? position.y - b.bottom() : 0.0f);
        if (d < distance)
        {
            distance = d;
            line_y   = b.y;
        }
    }
    // Along it: before the first item whose middle is past the point, else after the line's last.
    std::optional<std::size_t> last;
    for (const auto& [item, b] : cells)
    {
        if (std::abs(b.y - line_y) > 0.5f)
        {
            continue;
        }
        if (position.x < b.center().x)
        {
            return item;
        }
        last = item;
    }
    return last ? std::min(*last + 1, count_) : count_;
}

std::optional<std::size_t> ListViewBase::dragged_item() const
{
    return dragging_.empty() ? std::nullopt : std::optional<std::size_t>{dragging_.front()};
}

void ListViewBase::start_item_drag(std::size_t item, Point pointer)
{
    if (!can_drag_items_ || !is_enabled() || DragDrop::active())
    {
        return;
    }
    ListItemContainer* container = container_for_item(item);
    if (container == nullptr)
    {
        return;
    }
    // The selection travels when the item is part of it (Multiple/Extended), else the item alone.
    std::vector<std::size_t> dragged;
    if (selection_->is_selected(item) &&
        (selection_mode_ == SelectionMode::Multiple || selection_mode_ == SelectionMode::Extended))
    {
        dragged = selection_->selected();
    }
    else
    {
        dragged = {item};
    }
    DataPackage data;
    data.requested_operation = DataPackageOperation::Move;
    for (std::size_t i : dragged)
    {
        if (i < items_.size())
        {
            data.text += (data.text.empty() ? "" : "\n") + items_[i];
        }
    }
    data.data = ListViewDragPayload{this, dragged};
    DragItemsStartingArgs args{dragged, data};
    bool                  canceled = false;
    if (on_drag_items_starting_)
    {
        canceled = detail::canceled(on_drag_items_starting_(args));
    }
    if (canceled)
    {
        return;
    }
    RenderRecording recording;
    container->render(recording, last_theme_ != nullptr ? *last_theme_ : Theme::light());
    dragging_ = std::move(args.items);
    // The dragged item stays where it was, faded: a placeholder while the list can reorder, at
    // 0.8 opacity otherwise.
    for (std::size_t i : dragging_)
    {
        if (ListItemContainer* c = container_for_item(i))
        {
            c->set_opacity(can_reorder_items_ && allow_drop() ? 0.0f : 0.8f);
        }
    }
    DragDrop::start(*this, std::move(data), std::move(recording), container->bounds(), pointer);
}

void ListViewBase::update_reorder(std::optional<std::size_t> insert_before, float slot)
{
    gap_before_                           = insert_before;
    const std::optional<std::size_t> from = dragged_item();
    for (std::size_t flat : repeater_->realized_indices())
    {
        auto*                            c    = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat));
        const std::optional<std::size_t> item = flat_to_item(flat);
        if (c == nullptr || !item)
        {
            continue;
        }
        // From inside the list, each item between the dragged one and where it would go moves into
        // its neighbor's slot; from outside, everything from the insertion point moves on.
        int shift = 0;
        if (insert_before)
        {
            if (from)
            {
                if (*insert_before > *from + 1 && *item > *from && *item < *insert_before)
                {
                    shift = -1;
                }
                else if (*insert_before <= *from && *item >= *insert_before && *item < *from)
                {
                    shift = 1;
                }
            }
            else if (*item >= *insert_before)
            {
                shift = 1;
            }
        }
        Vector3 offset{};
        if (shift != 0)
        {
            const std::size_t neighbor = shift > 0 ? *item + 1 : *item - 1;
            if (const ListItemContainer* n = container_for_item(neighbor))
            {
                offset = Vector3{n->bounds().x - c->bounds().x, n->bounds().y - c->bounds().y, 0.0f};
            }
            else if (kind_ == Kind::List)
            {
                offset = Vector3{0.0f, static_cast<float>(shift) * slot, 0.0f};
            }
            // No container for the neighbor: its cell may be on the next line (the last item of a
            // line moving on) or the line before, so it is placed from the line geometry.
            else if (const std::optional<Point> to = tile_slot_position(neighbor))
            {
                offset = Vector3{to->x - c->bounds().x, to->y - c->bounds().y, 0.0f};
            }
            else
            {
                offset = Vector3{static_cast<float>(shift) * slot, 0.0f, 0.0f};
            }
        }
        c->set_translation_transition(Vector3Transition{Duration{ReorderMs}});
        c->set_translation(offset);
    }
}

std::optional<Point> ListViewBase::tile_slot_position(std::size_t item) const
{
    // The realized items in index order, with where their cells are.
    std::vector<std::pair<std::size_t, Point>> placed;
    for (std::size_t flat : repeater_->realized_indices())
    {
        const std::optional<std::size_t> i = flat_to_item(flat);
        if (const ListItemContainer* c = i ? container_for_item(*i) : nullptr)
        {
            placed.emplace_back(*i, Point{c->bounds().x, c->bounds().y});
        }
    }
    std::sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // Two line starts in a row of realized items: item 0, and every item that goes back along the
    // line and on to the next one compared to the item before it.
    std::vector<std::size_t> starts;
    for (std::size_t k = 0; k < placed.size() && starts.size() < 2; ++k)
    {
        const bool start = k == 0 ? placed[k].first == 0
                                  : placed[k].first == placed[k - 1].first + 1 &&
                                        ((placed[k].second.x < placed[k - 1].second.x &&
                                          placed[k].second.y > placed[k - 1].second.y) ||
                                         (placed[k].second.y < placed[k - 1].second.y &&
                                          placed[k].second.x > placed[k - 1].second.x));
        if (start)
        {
            starts.push_back(k);
        }
    }
    if (starts.size() < 2)
    {
        return std::nullopt;
    }
    const std::size_t first    = starts[0];
    const std::size_t per_line = placed[starts[1]].first - placed[first].first;
    // Every item of the first line is realized, so its cells give the place along a line.
    if (starts[1] - first != per_line)
    {
        return std::nullopt;
    }
    const auto distance         = static_cast<std::ptrdiff_t>(item) - static_cast<std::ptrdiff_t>(placed[first].first);
    const auto count            = static_cast<std::ptrdiff_t>(per_line);
    const std::ptrdiff_t line   = distance >= 0 ? distance / count : -((-distance + count - 1) / count);
    const auto           across = static_cast<std::size_t>(distance - line * count);
    const Point          step{placed[starts[1]].second.x - placed[first].second.x,
                              placed[starts[1]].second.y - placed[first].second.y};
    const Point          cell = placed[first + across].second;
    return Point{cell.x + static_cast<float>(line) * step.x, cell.y + static_cast<float>(line) * step.y};
}

void ListViewBase::clear_reorder(bool animate)
{
    gap_before_.reset();
    for (std::size_t flat : repeater_->realized_indices())
    {
        if (auto* c = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat)))
        {
            // A drop has moved the items to their new cells already, so they go there at once;
            // with nothing dropped they slide back from wherever the gap had them
            c->set_translation_transition(
                animate ? std::optional<Vector3Transition>{Vector3Transition{Duration{ReorderMs}}} : std::nullopt);
            c->set_translation(Vector3{});
        }
    }
}

void ListViewBase::drag_enter(const DragEventArgs& e)
{
    Element::drag_enter(e);
}

float ListViewBase::reorder_slot() const
{
    if (const std::optional<std::size_t> from = dragged_item())
    {
        if (const ListItemContainer* c = container_for_item(*from))
        {
            return kind_ == Kind::List ? c->bounds().height + container_margin_.vertical()
                                       : c->bounds().width + container_margin_.horizontal();
        }
    }
    return ListItemMinHeight;
}

DragResponse ListViewBase::drag_over(const DragEventArgs& e)
{
    const auto* payload = std::any_cast<ListViewDragPayload>(&e.data.data);
    const bool  own     = payload != nullptr && payload->source == this;
    if (own && can_reorder_items_ && allow_drop())
    {
        update_reorder(insertion_index_at(e.position), reorder_slot());
        drag_point_ = e.position;
        request_tick(); // near an edge, the view scrolls (drag_scroll())
        return DragResponse{.operation = DataPackageOperation::Move};
    }
    DragResponse response = Element::drag_over(e);
    if (can_reorder_items_ && response.operation != DataPackageOperation::None)
    {
        update_reorder(insertion_index_at(e.position), reorder_slot());
    }
    else if (gap_before_)
    {
        clear_reorder(true);
    }
    if (response.operation != DataPackageOperation::None)
    {
        drag_point_ = e.position;
        request_tick();
    }
    else
    {
        drag_point_.reset();
    }
    return response;
}

void ListViewBase::drag_leave(const DragEventArgs& e)
{
    drag_point_.reset();
    clear_reorder(true);
    Element::drag_leave(e);
}

bool ListViewBase::drag_scroll(Duration delta)
{
    const bool  vertical = scroll_->content_orientation() != ScrollContentOrientation::Horizontal;
    const Rect  view     = scroll_->bounds();
    const float at       = vertical ? drag_point_->y : drag_point_->x;
    const float start    = vertical ? view.top() : view.left();
    const float end      = vertical ? view.bottom() : view.right();
    const float band     = std::min(DragScrollBand, (end - start) / 4.0f);
    if (band <= 0.0f)
    {
        return false;
    }
    float direction = 0.0f; // -1 at the start's rim .. 1 at the end's
    if (at < start + band)
    {
        direction = -std::min(1.0f, (start + band - at) / band);
    }
    else if (at > end - band)
    {
        direction = std::min(1.0f, (at - (end - band)) / band);
    }
    if (direction == 0.0f)
    {
        return false;
    }
    const float before = vertical ? scroll_->vertical_offset() : scroll_->horizontal_offset();
    const float limit  = vertical ? scroll_->scrollable_height() : scroll_->scrollable_width();
    const float after  = std::clamp(before + direction * DragScrollSpeed * static_cast<float>(delta.count() / 1000.0),
                                    0.0f,
                                    limit);
    if (after == before)
    {
        return false;
    }
    request_tick(); // a pointer held still in the band moves nothing itself
    if (vertical)
    {
        scroll_->jump_to_vertical_offset(after);
    }
    else
    {
        scroll_->jump_to_horizontal_offset(after);
    }
    if (gap_before_)
    {
        update_reorder(insertion_index_at(*drag_point_), reorder_slot());
    }
    return true;
}

void ListViewBase::drop(const DragEventArgs& e)
{
    drag_point_.reset();
    const auto* payload = std::any_cast<ListViewDragPayload>(&e.data.data);
    const bool  own     = payload != nullptr && payload->source == this;
    if (!(own && can_reorder_items_ && allow_drop()))
    {
        clear_reorder();
        Element::drop(e);
        return;
    }
    const std::size_t insert = gap_before_ ? *gap_before_ : insertion_index_at(e.position);
    clear_reorder();
    std::vector<std::size_t> moving = payload->items;
    std::sort(moving.begin(), moving.end());
    moving.erase(std::unique(moving.begin(), moving.end()), moving.end());
    const auto before = static_cast<std::size_t>(
        std::count_if(moving.begin(), moving.end(), [&](std::size_t i) { return i < insert; }));
    const std::size_t        dest = insert - before;
    std::vector<std::size_t> order(count_);
    std::iota(order.begin(), order.end(), std::size_t{0});
    for (std::size_t k = 0; k < moving.size(); ++k)
    {
        const std::size_t target = std::min(dest + k, count_ - 1);
        const auto pos = static_cast<std::size_t>(std::find(order.begin(), order.end(), moving[k]) - order.begin());
        if (pos == target)
        {
            continue;
        }
        const std::size_t id = order[pos];
        order.erase(order.begin() + static_cast<std::ptrdiff_t>(pos));
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(target), id);
        if (!items_.empty())
        {
            move_item(pos, target);
        }
        else
        {
            notify_moved(pos, target);
        }
        if (on_items_reordered_)
        {
            on_items_reordered_(ItemsReorderedArgs{.from = pos, .to = target});
        }
    }
    dragging_.clear();
    for (std::size_t k = 0; k < moving.size(); ++k)
    {
        dragging_.push_back(std::min(dest + k, count_ - 1));
    }
}

void ListViewBase::drag_completed(DataPackageOperation result)
{
    for (std::size_t flat : repeater_->realized_indices())
    {
        if (auto* c = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat)))
        {
            c->set_opacity(1.0f);
            c->reset_pointer();
        }
    }
    // A canceled drag slides the items back; after a drop they're in place
    clear_reorder(result == DataPackageOperation::None);
    DragItemsCompletedArgs args{.items = std::move(dragging_), .result = result};
    dragging_.clear();
    press_item_.reset();
    if (on_drag_items_completed_)
    {
        on_drag_items_completed_(args);
    }
}

// ---- transitions ----

void ListViewBase::open_batch(bool reset)
{
    if (batch_.open)
    {
        batch_.reset = batch_.reset || reset;
        return;
    }
    batch_             = Batch{};
    batch_.open        = true;
    batch_.reset       = reset;
    const Point origin = repeater_->bounds().origin();
    for (std::size_t flat : repeater_->realized_indices())
    {
        const Element*                   e    = repeater_->try_get_element(flat);
        const std::optional<std::size_t> item = flat_to_item(flat);
        if (e == nullptr || !item || *item >= ids_.size())
        {
            continue;
        }
        batch_.before[ids_[*item]] = Point{e->bounds().x - origin.x, e->bounds().y - origin.y};
        // A new collection: what shows fades out as the new items come in (ContentTransition).
        if (reset && find_transition<ContentTransition>(item_transitions_) != nullptr && last_theme_ != nullptr &&
            last_theme_->animations_enabled && animations_enabled() && e->bounds().intersects(scroll_->bounds()))
        {
            Ghost g;
            e->render(g.recording, *last_theme_);
            g.recorded_at = e->bounds();
            g.offset      = Point{e->bounds().x - origin.x, e->bounds().y - origin.y};
            g.storyboard  = transition_storyboards::content_out();
            ghosts_.push_back(std::move(g));
        }
    }
}

void ListViewBase::add_ghost(std::size_t item)
{
    if (find_transition<AddRemoveTransition>(item_transitions_) == nullptr || last_theme_ == nullptr ||
        !last_theme_->animations_enabled || !animations_enabled())
    {
        return;
    }
    const ListItemContainer* c = container_for_item(item);
    if (c == nullptr)
    {
        return;
    }
    // A removed item shrinks away where it was (DELETEFROMGRID), drawn from a recording of itself.
    const Point origin = repeater_->bounds().origin();
    Ghost       g;
    c->render(g.recording, *last_theme_);
    g.recorded_at = c->bounds();
    g.offset      = Point{c->bounds().x - origin.x, c->bounds().y - origin.y};
    g.storyboard  = transition_storyboards::remove();
    ghosts_.push_back(std::move(g));
}

void ListViewBase::resolve_batch()
{
    const Point origin         = repeater_->bounds().origin();
    const auto  realized_items = [&]
    {
        std::vector<std::pair<std::size_t, ListItemContainer*>> out;
        for (std::size_t flat : repeater_->realized_indices())
        {
            auto*                            c    = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat));
            const std::optional<std::size_t> item = flat_to_item(flat);
            if (c != nullptr && item)
            {
                out.emplace_back(*item, c);
            }
        }
        return out;
    };
    // EntranceTransition when the items first appear.
    if (!entered_ && repeater_->realized_count() > 0)
    {
        entered_ = true;
        if (const auto* entrance = find_transition<EntranceTransition>(item_transitions_);
            entrance != nullptr && !batch_.open)
        {
            std::size_t n = 0;
            for (auto& [item, c] : realized_items())
            {
                TransitionStoryboard sb = transition_storyboards::entrance(Point{entrance->offset_x, entrance->offset_y});
                if (entrance->stagger)
                {
                    sb.delay = stagger_delay(n++, Duration{35.0});
                }
                c->run_transition(std::move(sb));
            }
        }
    }
    if (!batch_.open)
    {
        return;
    }
    const Batch batch = std::move(batch_);
    batch_            = Batch{};
    if (batch.reset)
    {
        std::size_t n = 0;
        for (auto& [item, c] : realized_items())
        {
            if (const auto* content = find_transition<ContentTransition>(item_transitions_))
            {
                c->run_transition(transition_storyboards::content_in(Point{content->offset_x, content->offset_y}));
            }
            else if (const auto* entrance = find_transition<EntranceTransition>(item_transitions_))
            {
                TransitionStoryboard sb = transition_storyboards::entrance(Point{entrance->offset_x, entrance->offset_y});
                if (entrance->stagger)
                {
                    sb.delay = stagger_delay(n++, Duration{35.0});
                }
                c->run_transition(std::move(sb));
            }
        }
        return;
    }
    if (find_transition<AddRemoveTransition>(item_transitions_) == nullptr)
    {
        return;
    }
    // The layout-transition delays for lists: pushed items move before an add, a
    // deletion's gap closes after it, and an add waits for both when both happen at once.
    std::size_t added   = 0;
    bool        removed = !ghosts_.empty();
    for (auto& [item, c] : realized_items())
    {
        if (item < ids_.size() && !batch.before.contains(ids_[item]))
        {
            ++added;
        }
    }
    const bool     mixed = added > 0 && removed;
    const Duration add_delay{mixed ? 600.0 : 300.0};
    const Duration move_time{removed ? 333.0 : 400.0};
    const Duration move_delay{removed ? 220.0 : 0.0};
    for (auto& [item, c] : realized_items())
    {
        if (item >= ids_.size())
        {
            continue;
        }
        const auto old = batch.before.find(ids_[item]);
        if (old == batch.before.end())
        {
            c->run_transition(transition_storyboards::add(add_delay));
            continue;
        }
        const Point now{c->bounds().x - origin.x, c->bounds().y - origin.y};
        const Point moved{old->second.x - now.x, old->second.y - now.y};
        if (moved.x == 0.0f && moved.y == 0.0f)
        {
            continue;
        }
        // From where it is drawn now, which may be partway through an earlier move.
        const TransitionTargetState& at = c->transition_target();
        c->run_transition(
            transition_storyboards::make_room(Point{moved.x + at.translate.x, moved.y + at.translate.y}, move_time, move_delay));
    }
}

// ---- sticky headers ----

void ListViewBase::update_sticky_header()
{
    sticky_h_ = 0.0f;
    // Only a view that scrolls vertically pins its headers at the top.
    if (!sticky_headers_ || !grouped() || group_names_.empty() ||
        scroll_->content_orientation() != ScrollContentOrientation::Vertical || last_measurer_ == nullptr ||
        last_theme_ == nullptr)
    {
        sticky_group_.reset();
        return;
    }
    const float                view_top = scroll_->bounds().y;
    std::optional<std::size_t> group;
    std::size_t                flat_top = 0;
    for (std::size_t flat : repeater_->realized_indices())
    {
        const Element* e = repeater_->try_get_element(flat);
        if (e != nullptr && e->bounds().bottom() > view_top)
        {
            flat_top = flat;
            break;
        }
    }
    std::size_t header_pos = 0;
    for (std::size_t g = 0; g < group_names_.size(); ++g)
    {
        if (flat_top < group_flat_[g] + 1 + group_sizes_[g])
        {
            group      = g;
            header_pos = group_flat_[g];
            break;
        }
    }
    if (!group)
    {
        sticky_group_.reset();
        return;
    }
    if (const Element* header = repeater_->try_get_element(header_pos); header != nullptr && header->bounds().y >= view_top)
    {
        sticky_group_.reset();
        return;
    }
    if (sticky_group_ != group || !sticky_)
    {
        std::unique_ptr<Element> content = group_header_template_ ? group_header_template_(*group) : nullptr;
        if (!content)
        {
            content = std::make_unique<GroupHeaderText>(group_key(*group));
        }
        sticky_ = std::make_unique<ListItemContainer>(*this, ListItemContainer::Role::GroupHeader, std::move(content));
        set_parent_of(*sticky_, this);
        sticky_group_ = group;
    }
    const float width = scroll_->bounds().width;
    const Size  size  = sticky_->measure(Size{width, Unbounded}, *last_measurer_, *last_theme_);
    // Pinned at the top until the next group's header pushes it up.
    float bottom = std::numeric_limits<float>::max();
    if (*group + 1 < group_names_.size())
    {
        if (const Element* next = repeater_->try_get_element(group_flat_[*group + 1]))
        {
            bottom = next->bounds().y - next->margin().top;
        }
    }
    sticky_y_ = std::min(view_top, bottom - size.height);
    sticky_h_ = std::max(0.0f, sticky_y_ + size.height - view_top);
    sticky_->arrange(Rect{scroll_->bounds().x, sticky_y_, width, size.height});
}

// ---- layout ----

Size ListViewBase::measure_override(Size available, const MeasureContext& context) const
{
    last_measurer_        = &context.measurer;
    last_theme_           = &context.theme;
    const Thickness inset = border_thickness_;
    const Size      room{std::max(0.0f, available.width - inset.horizontal()),
                         std::max(0.0f, available.height - inset.vertical())};
    const Size      d = scroll_->measure(room, context);
    return Size{d.width + inset.horizontal(), d.height + inset.vertical()};
}

void ListViewBase::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    scroll_->arrange(final_rect.inset(border_thickness_));
    resolve_batch();
    if (keep_last_pending_ && scroll_->bounds().height > 0.0f)
    {
        keep_last_pending_ = false;
        // At once, inside this arrange.
        scroll_->jump_to_vertical_offset(scroll_->scrollable_height());
    }
    update_sticky_header();
}

// ---- render ----

std::optional<Rect> ListViewBase::ring_item_rect() const
{
    const std::optional<std::size_t> item = scope_entered_ && focus_index_ != detail::NoIndex
                                                ? std::optional<std::size_t>{focus_index_}
                                                : current_;
    const ListItemContainer*         c    = item ? container_for_item(*item) : nullptr;
    if (c == nullptr)
    {
        return std::nullopt;
    }
    const Rect visible = intersection(c->bounds(), scroll_->bounds());
    return visible.empty() ? std::nullopt : std::optional<Rect>{visible};
}

Rect ListViewBase::focus_ring_rect(const Theme& /*theme*/) const
{
    return ring_item_rect().value_or(bounds());
}

float ListViewBase::focus_ring_corner_radius(const Theme& theme) const
{
    return ring_item_rect() ? theme.corner_radius : 0.0f;
}

bool ListViewBase::focus_ring_inside() const
{
    return ring_item_rect().has_value();
}

void ListViewBase::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    last_theme_        = &theme;
    draw_border(renderer, bounds(), background_.resolve(theme), border_brush_.resolve(theme), border_thickness_, 0.0f);

    const Rect view   = scroll_->bounds();
    const bool pinned = sticky_ && sticky_group_ && sticky_h_ > 0.0f;
    if (pinned)
    {
        // The items do not show under a pinned header.
        renderer.push_clip(Rect{view.x, view.y + sticky_h_, view.width, std::max(0.0f, view.height - sticky_h_)});
    }
    scroll_->render(renderer, context);
    if (pinned)
    {
        renderer.pop_clip();
    }

    if (!ghosts_.empty())
    {
        const Point origin = repeater_->bounds().origin();
        renderer.push_clip(view);
        for (const Ghost& g : ghosts_)
        {
            const TransitionTargetState target = sample_transition(g.storyboard, g.elapsed);
            if (target.opacity <= 0.0f)
            {
                continue;
            }
            const Rect at{origin.x + g.offset.x, origin.y + g.offset.y, g.recorded_at.width, g.recorded_at.height};
            renderer.push_transform(1.0f, Point{at.x - g.recorded_at.x, at.y - g.recorded_at.y});
            const Matrix transform   = transition_transform(target, g.recorded_at);
            const bool   transformed = !transform.is_identity();
            if (transformed)
            {
                renderer.push_transform(transform);
            }
            renderer.push_opacity_layer(target.opacity);
            g.recording.replay(renderer);
            renderer.pop_opacity_layer();
            if (transformed)
            {
                renderer.pop_transform();
            }
            renderer.pop_transform();
        }
        renderer.pop_clip();
    }

    if (pinned)
    {
        renderer.push_clip(view);
        sticky_->render(renderer, context);
        renderer.pop_clip();
    }
}

// ---- input ----

bool ListViewBase::pointer_override(const PointerEvent& e)
{
    // Here first only over a pinned header (covers_children_at()), where a press goes nowhere.
    return e.action == PointerAction::Down && covers_children_at(e.position);
}

bool ListViewBase::covers_children_at(Point p) const
{
    // A pinned header covers the rows under it (the scroll bar's strip excepted).
    if (header_ != nullptr || footer_ != nullptr || !sticky_ || !sticky_group_ || sticky_h_ <= 0.0f)
    {
        return false;
    }
    const Rect view = scroll_->bounds();
    return p.y >= view.y && p.y < view.y + sticky_h_ && p.x < view.right() - 16.0f;
}

bool ListViewBase::takes_arrow_key(Key key) const
{
    if (kind_ == Kind::Grid)
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    return key == Key::ArrowUp || key == Key::ArrowDown;
}

void ListViewBase::focus_changed(bool focused)
{
    if (focused && !current_ && count_ > 0)
    {
        // Focus arrives on the selected item, else the first (the last in a list kept at its end).
        current_ = selected_index().value_or(updating_mode_ == ItemsUpdatingScrollMode::KeepLastItemInView ? count_ - 1 : 0);
    }
    mark_dirty(DirtyFlags::Visual);
}

bool ListViewBase::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || count_ == 0 || e.action != KeyAction::Down)
    {
        return handled;
    }
    if (scope_entered_)
    {
        // Entered as a focus scope the arrows move a cursor and Enter/Space commit it, so stepping
        // through a navigation pane does not navigate on every key.
        switch (e.key)
        {
            case Key::ArrowUp:
                (void)scope_step(-1);
                break;
            case Key::ArrowDown:
                (void)scope_step(+1);
                break;
            case Key::Home:
                focus_index_ = item_at_rank(0);
                scroll_into_view(focus_index_);
                mark_dirty(DirtyFlags::Visual);
                break;
            case Key::End:
                focus_index_ = item_at_rank(count_ - 1);
                scroll_into_view(focus_index_);
                mark_dirty(DirtyFlags::Visual);
                break;
            case Key::Enter:
            case Key::Space:
                if (focus_index_ != detail::NoIndex)
                {
                    set_selected_index(focus_index_);
                }
                break;
            default:
                break;
        }
        return handled;
    }

    const std::size_t from = current_.value_or(selected_index().value_or(0));
    switch (e.key)
    {
        case Key::Enter:
        case Key::Space:
            set_current(from);
            if (item_click_enabled_ && on_item_click_)
            {
                on_item_click_(from);
            }
            if (e.key == Key::Space && e.modifiers.ctrl)
            {
                secondary_gesture(from, e.modifiers.shift);
            }
            else
            {
                primary_gesture(from, e.modifiers.shift);
            }
            // on_item_invoked: Enter always, Space only when nothing can be selected.
            if (e.key == Key::Enter || selection_mode_ == SelectionMode::None)
            {
                invoke_item(from);
            }
            return handled;
        case Key::A:
            if (e.modifiers.ctrl &&
                (selection_mode_ == SelectionMode::Multiple || selection_mode_ == SelectionMode::Extended))
            {
                // Ctrl+A: everything, or nothing when everything already is.
                if (selection_->all_selected())
                {
                    deselect_all();
                }
                else
                {
                    select_all();
                }
            }
            return handled;
        case Key::ArrowUp:
        case Key::ArrowDown:
        case Key::ArrowLeft:
        case Key::ArrowRight:
        case Key::Home:
        case Key::End:
        case Key::PageUp:
        case Key::PageDown:
            if (const std::optional<std::size_t> to = neighbor(from, e.key))
            {
                keyboard_moved(*to, e.modifiers.ctrl, e.modifiers.shift);
            }
            return handled;
        default:
            return handled;
    }
}

bool ListViewBase::raise_context_requested(std::optional<Point> position)
{
    ListItemContainer* target = nullptr;
    if (position)
    {
        for (std::size_t flat : repeater_->realized_indices())
        {
            auto* c = dynamic_cast<ListItemContainer*>(repeater_->try_get_element(flat));
            if (c != nullptr && c->role() == ListItemContainer::Role::Item && c->bounds().contains(*position))
            {
                target = c;
                break;
            }
        }
    }
    else if (current_)
    {
        target = container_for_item(*current_);
    }
    if (target != nullptr)
    {
        if (const std::optional<std::size_t> item = target->item())
        {
            set_current(*item);
        }
        if (raise_context_requested_in(*target, position))
        {
            return true;
        }
        const std::optional<std::size_t> item = target->item();
        if (item_context_menu_ && item && *item < count_)
        {
            std::vector<MenuItem> entries = item_context_menu_(*item);
            if (!entries.empty())
            {
                if (selection_mode_ != SelectionMode::None && !is_selected(*item))
                {
                    set_selected_index(*item);
                }
                detail::show_context_menu(item_menu_, *this, std::move(entries), position.value_or(target->bounds().center()));
                return true;
            }
        }
    }
    return Element::raise_context_requested(position);
}

std::optional<Element::ScrollInfo> ListViewBase::scroll_info() const
{
    // A ListView scrolls with a Scroll, which can be pulled a tenth of its viewport past either end.
    std::optional<ScrollInfo> info = scroll_->scroll_info();
    if (info)
    {
        info->out_of_bounds_pan = Size{info->viewport.width * 0.1f, info->viewport.height * 0.1f};
    }
    return info;
}

std::string ListViewBase::text_value() const
{
    const std::optional<std::size_t> selected = selected_index();
    return selected && *selected < items_.size() ? items_[*selected] : std::string{};
}

void ListViewBase::selected_items(std::vector<Element*>& out) const
{
    for (const std::size_t item : selected_items())
    {
        if (Element* container = repeater_->try_get_element(item_to_flat(item)))
        {
            out.push_back(container);
        }
    }
}

// ---- focus scope ----

void ListViewBase::collect_focusables(std::vector<Element*>& out)
{
    if (header_ != nullptr)
    {
        header_->collect_focusables(out);
    }
    if (focusable())
    {
        out.push_back(this);
    }
    if (footer_ != nullptr)
    {
        footer_->collect_focusables(out);
    }
}

void ListViewBase::collect_scope_focusables(std::vector<Element*>& out)
{
    // An item-navigating scope offers only itself; an empty or disabled list declines.
    if (focusable())
    {
        out.push_back(this);
    }
}

bool ListViewBase::scope_step(int delta)
{
    if (count_ == 0)
    {
        return false;
    }
    const std::size_t from = focus_index_ == detail::NoIndex ? 0 : rank_of(focus_index_);
    focus_index_           = item_at_rank(delta >= 0 ? (from + 1) % count_ : (from == 0 ? count_ - 1 : from - 1));
    scroll_into_view(focus_index_);
    mark_dirty(DirtyFlags::Visual);
    return true;
}

void ListViewBase::set_scope_entered(bool entered)
{
    if (!entered && selected_index())
    {
        current_ = selected_index(); // back to one tab stop whose arrows go on from the selection
    }
    scope_entered_ = entered;
    focus_index_   = entered ? selected_index().value_or(0) : detail::NoIndex;
    if (entered)
    {
        scroll_into_view(focus_index_);
    }
    mark_dirty(DirtyFlags::Visual);
}

// ---- tree plumbing ----

void ListViewBase::clear_dirty_override(DirtyFlags flags)
{
    if (sticky_)
    {
        clear_dirty_of(*sticky_, flags);
    }
    if (item_menu_)
    {
        clear_dirty_of(*item_menu_, flags); // listed as a child only while it shows
    }
}

void ListViewBase::tick_override(Duration delta)
{
    if (drag_point_ && drag_scroll(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (!ghosts_.empty())
    {
        for (Ghost& g : ghosts_)
        {
            g.elapsed += animation_delta(delta);
        }
        std::erase_if(ghosts_, [](const Ghost& g) { return g.elapsed >= g.storyboard.duration(); });
        mark_dirty(DirtyFlags::Visual);
    }
    if (background_.advance(delta) || border_brush_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (sticky_)
    {
        sticky_->tick(delta);
    }
}

} // namespace ui

// SPDX-License-Identifier: MIT
#include "winui/ui/collections/TableView.hpp"

#include "../menus/ContextMenuPopup.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/VirtualListControl.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cmath>
#include <compare>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float TableViewHoverOverlayAlpha = 0.10f;
// The banded row of a theme without the tabular palette: a faint wash of its own text color.
constexpr float EraBandingAlpha = 0.06f;

// The cell and header cell padding, applied only with the subtle-selection
// package; the era packs keep their own theme.padding.
constexpr Thickness CellPadding       = Symmetric{.horizontal = 8.0f, .vertical = 4.0f};
constexpr Thickness HeaderCellPadding = Symmetric{.horizontal = 8.0f, .vertical = 4.0f};

// The selected row's accent border.
constexpr float SelectedBorderThickness = 2.0f;
// The table's own border, in the grid line color, round the header and the rows.
constexpr float BorderThickness = 1.0f;
// The strip at a header's trailing edge that drags its width.
constexpr float ResizeGripperWidth = 8.0f;
// The sort chevron's box at the header's trailing edge,
// and the glyph inside it.
constexpr float SortIndicatorSize      = 16.0f;
constexpr float SortIndicatorGlyphSize = 12.0f;
// ChevronUpSmall / ChevronDownSmall.
constexpr IconGlyph SortAscendingGlyph{0xE96D};
constexpr IconGlyph SortDescendingGlyph{0xE96E};
// How far Left/Right scroll the columns sideways.
constexpr float HorizontalKeyStep = 40.0f;
// How far Ctrl+Left/Right on a column header narrows or widens the column.
constexpr float HeaderKeyResizeStep = 8.0f;

// The group header row: its padding, a 24 DIP gutter holding a 12 DIP chevron, 6 DIP between
// the gutter, the key and the count, and the count's opacity.
constexpr Thickness GroupHeaderPadding    = Edges{.left = 8.0f, .top = 6.0f, .right = 16.0f, .bottom = 6.0f};
constexpr float     GroupExpanderSize     = 24.0f;
constexpr float     GroupHeaderSpacing    = 6.0f;
constexpr float     GroupChevronGlyphSize = 12.0f;
constexpr float     GroupCountOpacity     = 0.7f;
// ChevronDown while expanded, ChevronRight while collapsed.
constexpr IconGlyph GroupExpandedGlyph{0xE70D};
constexpr IconGlyph GroupCollapsedGlyph{0xE76C};
// The group header band: the default layer fill in Dark, a touch darker than the rows in Light.
constexpr Color GroupHeaderBackgroundDark  = Color::from_argb(0x4C3A3A3A);
constexpr Color GroupHeaderBackgroundLight = Color::from_argb(0x14000000);

// `CharacterEllipsis`: the longest start of `text` that still fits `width` with an ellipsis after
// it, cut between clusters. Text that fits comes back unchanged. Headers and cells both trim, so
// a header too wide for its column degrades the same way as the cells under it.
[[nodiscard]] std::string trim_to_width(const std::string& text, const TextStyle& style, float width, const TextMeasurer& measurer)
{
    const auto fits = [&](std::string_view s) { return measurer.measure(s, style).width <= width + 0.5f; };
    if (text.empty() || fits(text))
    {
        return text;
    }
    std::vector<std::size_t> stops{0};
    for (std::size_t i = 0; i < text.size();)
    {
        i = text_analyzer().next_cluster(text, i);
        stops.push_back(i);
    }
    std::size_t lo = 0;
    std::size_t hi = stops.size() - 1;
    while (lo < hi)
    {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (fits(text.substr(0, stops[mid]) + "…"))
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    std::size_t cut = stops[lo];
    while (cut > 0 && text[cut - 1] == ' ')
    {
        --cut;
    }
    return text.substr(0, cut) + "…";
}

// `wrapped`: a content-sized row's cells wrap instead of being trimmed.
[[nodiscard]] TextStyle cell_text_style(const Theme& theme, bool wrapped = false)
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    if (wrapped)
    {
        style.wrapping = TextWrapping::Wrap;
    }
    return style;
}

// The header text is SemiBold under the tabular (Fluent) row model; the era packs draw their
// header captions in the regular weight their list headers always had.
[[nodiscard]] TextStyle header_text_style(const Theme& theme)
{
    TextStyle style = cell_text_style(theme);
    if (detail::controls(theme).list_item_subtle_selection)
    {
        style.weight = FontWeight::SemiBold;
    }
    return style;
}

// A group header's key is SemiBold everywhere.
[[nodiscard]] TextStyle group_key_style(const Theme& theme)
{
    TextStyle style = cell_text_style(theme);
    style.weight    = FontWeight::SemiBold;
    return style;
}

[[nodiscard]] Thickness cell_padding(const Theme& theme)
{
    return detail::controls(theme).list_item_subtle_selection ? CellPadding : theme.padding;
}

[[nodiscard]] Thickness header_padding(const Theme& theme)
{
    return detail::controls(theme).list_item_subtle_selection ? HeaderCellPadding : theme.padding;
}

[[nodiscard]] Color grid_line_color(const Theme& theme)
{
    return detail::controls(theme).list_item_subtle_selection ? theme.palette.tabular_grid_line : theme.surfaces.separator_dark;
}

[[nodiscard]] float clamp_width(float width, const TableViewColumn& column)
{
    const float lo = std::max(0.0f, column.min_width);
    return std::clamp(width, lo, std::max(lo, column.max_width));
}

// A whole string read as a number, or nullopt.
[[nodiscard]] std::optional<double> as_number(const std::string& text)
{
    if (text.empty())
    {
        return std::nullopt;
    }
    double      value    = 0.0;
    const char* end      = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || std::isnan(value))
    {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] char fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// A group header's item count, in parentheses.
[[nodiscard]] std::string group_count_text(std::size_t count)
{
    return std::format("({})", count);
}

const std::string EmptyText;
} // namespace

// ---- automation nodes ----
// A text cell is drawn by its row and has no Element, and a template cell's Element belongs to the
// table, so each cell of a row in view gets one of these when a client first asks for it. Its row
// owns it.
class TableViewCellAutomation final : public Element, private GridItemProvider, private TableItemProvider
{
public:
    TableViewCellAutomation(TableViewControl* owner, Element* row, std::size_t position, std::size_t column) :
        owner_(owner),
        position_(position),
        column_(column)
    {
        set_parent_of(*this, row);
    }

    void place(const Rect& rect)
    {
        detail::LayoutAccess::set_bounds(*this, rect);
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return content() != nullptr ? AutomationRole::Custom : AutomationRole::Text;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        if (Element* shown = content())
        {
            out.push_back(shown);
        }
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return owner_->cell_text(owner_->row_at(position_), column_);
    }

    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    // The open editor or the template Element this cell shows, if any.
    [[nodiscard]] Element* content() const
    {
        const std::size_t row = owner_->row_at(position_);
        if (owner_->editor_ != nullptr && owner_->edit_row_ == row && owner_->edit_column_ == column_)
        {
            return owner_->editor_.get();
        }
        if (const TableViewControl::RealizedRow* realized = owner_->realized(row);
            realized != nullptr && column_ < realized->cells.size())
        {
            return realized->cells[column_].get();
        }
        return nullptr;
    }

    TableViewControl* owner_;
    std::size_t       position_;
    std::size_t       column_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.grid_item  = this;
        out.table_item = this;
    }
    [[nodiscard]] std::size_t grid_item_row() const override
    {
        return position_;
    }
    [[nodiscard]] std::size_t grid_item_column() const override
    {
        return column_;
    }
    [[nodiscard]] Element* grid_item_container() const override
    {
        return owner_;
    }
    void table_item_column_headers(std::vector<Element*>& out) const override
    {
        if (Element* header = owner_->header_item_automation(column_))
        {
            out.push_back(header);
        }
    }
};

// One column header, under the header strip's node. Invoke sorts by it, as a click does.
class TableViewHeaderItemAutomation final : public Element, private InvokeProvider
{
public:
    TableViewHeaderItemAutomation(TableViewControl* owner, Element* header, std::size_t column) :
        owner_(owner),
        column_(column)
    {
        set_parent_of(*this, header);
    }

    void place(const Rect& rect)
    {
        detail::LayoutAccess::set_bounds(*this, rect);
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::HeaderItem;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        (void)out;
    }

protected:
    void automation_providers(AutomationProviders& out) override
    {
        if (sortable())
        {
            out.invoke = this;
        }
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return column_ < owner_->columns_.size() ? owner_->columns_[column_].header : std::string{};
    }

    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    [[nodiscard]] bool sortable() const
    {
        return owner_->can_user_sort_columns_ && column_ < owner_->columns_.size() && owner_->columns_[column_].can_sort;
    }

    TableViewControl* owner_;
    std::size_t       column_;

private:
    bool invoke() override
    {
        if (!sortable() || !owner_->is_enabled())
        {
            return false;
        }
        (void)owner_->toggle_sort_direction(column_);
        return true;
    }
};

// The header strip: a Header node over one HeaderItem per column.
class TableViewHeaderAutomation final : public Element
{
public:
    explicit TableViewHeaderAutomation(TableViewControl* owner) : owner_(owner)
    {
        set_parent_of(*this, owner);
    }

    // Matches the items to the columns and places everything over the strip.
    void sync()
    {
        const std::size_t count = owner_->columns_.size();
        while (items_.size() > count)
        {
            items_.pop_back();
        }
        while (items_.size() < count)
        {
            items_.push_back(std::make_unique<TableViewHeaderItemAutomation>(owner_, this, items_.size()));
        }
        const Rect strip = owner_->bounds();
        detail::LayoutAccess::set_bounds(*this, Rect{strip.left(), strip.top(), strip.width, owner_->header_height_});
        for (std::size_t c = 0; c < count; ++c)
        {
            items_[c]->place(
                Rect{strip.left() + owner_->column_left(c), strip.top(), owner_->column_actual_width(c), owner_->header_height_});
        }
    }

    [[nodiscard]] Element* item(std::size_t column) const
    {
        return column < items_.size() ? items_[column].get() : nullptr;
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Header;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        for (const auto& item : items_)
        {
            out.push_back(item.get());
        }
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    TableViewControl*                                           owner_;
    std::vector<std::unique_ptr<TableViewHeaderItemAutomation>> items_;
};

// Reads selection, column geometry and cell text live from `owner_` on every render() instead of
// baking them in, as a list row does. `position_` is the row's place in the sorted, grouped view; a
// position the owner reports as a group header draws that header instead of cells.
//
// A template cell or an open editor is owned by the table, not by this row: VirtualList builds its
// rows again on every arrange and scroll step, and a check box or a TextBox must keep its state
// (and focus) across that. The row only measures, arranges, draws and routes input to them.
class TableViewRow final : public Element, private SelectionItemProvider, private ExpandCollapseProvider
{
public:
    TableViewRow(TableViewControl* owner, std::size_t position) : owner_(owner), position_(position)
    {
    }

    // ---- automation ----
    [[nodiscard]] std::size_t position() const
    {
        return position_;
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return header() ? AutomationRole::Group : AutomationRole::DataItem;
    }
    // A group header expands and collapses its group, a data row is selected.
    void automation_providers(AutomationProviders& out) override
    {
        if (header())
        {
            out.expand_collapse = this;
        }
        else
        {
            out.selection_item = this;
        }
    }
    [[nodiscard]] bool item_selected() const override
    {
        return !header() && owner_->is_row_selected(row());
    }
    bool select_item() override
    {
        if (header() || !owner_->is_enabled() || owner_->selection_mode() == SelectionMode::None)
        {
            return false;
        }
        owner_->set_selected_row(row());
        return true;
    }
    bool add_to_selection() override
    {
        const SelectionMode mode = owner_->selection_mode();
        if (mode == SelectionMode::Single)
        {
            return select_item();
        }
        if (header() || !owner_->is_enabled() || mode == SelectionMode::None)
        {
            return false;
        }
        std::vector<std::size_t> rows = owner_->selected_rows();
        rows.push_back(row());
        owner_->set_selected_rows(std::move(rows));
        return true;
    }
    bool remove_from_selection() override
    {
        if (header() || !owner_->is_enabled() || owner_->selection_mode() == SelectionMode::None)
        {
            return false;
        }
        std::vector<std::size_t> rows = owner_->selected_rows();
        std::erase(rows, row());
        owner_->set_selected_rows(std::move(rows));
        return true;
    }
    [[nodiscard]] bool expanded_state() const override
    {
        return header() && owner_->is_group_expanded(owner_->position_group(position_));
    }
    bool set_expanded_state(bool expanded) override
    {
        if (!header() || !owner_->is_enabled())
        {
            return false;
        }
        owner_->set_group_expanded(owner_->position_group(position_), expanded); // may rebuild this row
        return true;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        if (header())
        {
            if (Element* content = owner_->group_header_element(owner_->position_group(position_)))
            {
                out.push_back(content);
            }
            return;
        }
        for (std::size_t c = 0; c < owner_->columns_.size(); ++c)
        {
            out.push_back(cell_automation(c));
        }
    }
    // The node of cell `column`, built with the rest of the row's on first use.
    [[nodiscard]] TableViewCellAutomation* cell_automation(std::size_t column) const
    {
        const std::size_t count = owner_->columns_.size();
        if (cell_automation_.size() != count)
        {
            cell_automation_.clear();
            auto* self = const_cast<TableViewRow*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
            for (std::size_t c = 0; c < count; ++c)
            {
                cell_automation_.push_back(std::make_unique<TableViewCellAutomation>(owner_, self, position_, c));
                cell_automation_.back()->place(
                    Rect{bounds().left() + owner_->column_left(c),
                         bounds().top(),
                         owner_->column_actual_width(c),
                         bounds().height});
            }
        }
        return column < cell_automation_.size() ? cell_automation_[column].get() : nullptr;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        if (header())
        {
            return owner_->group_key(owner_->position_group(position_));
        }
        std::string joined;
        for (const std::string& cell : cells_of(row()))
        {
            if (!joined.empty())
            {
                joined += ", ";
            }
            joined += cell;
        }
        return joined;
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        if (header())
        {
            render_group_header(renderer, theme);
            return;
        }
        const std::size_t row      = this->row();
        const bool        selected = owner_->is_row_selected(row);
        const bool        enabled  = owner_->is_enabled();
        // A disabled grid takes no pointer input, but a row may still remember the hover it had.
        const ButtonState state = !enabled   ? ButtonState::Disabled
                                  : pressed_ ? ButtonState::Pressed
                                  : hovered_ ? ButtonState::Hover
                                             : ButtonState::Normal;

        // The alternating row background, under every state fill.
        if (owner_->alternating_rows_ && position_ % 2 == 1)
        {
            const std::optional<Color> custom = owner_->alternating_background_.resolve(theme);
            const Color                band   = custom ? *custom
                                                : detail::controls(theme).list_item_subtle_selection
                                                    ? theme.palette.tabular_row_alternating
                                                    : theme.colors().text.with_alpha(EraBandingAlpha);
            renderer.fill_rounded_rect(bounds(), 0.0f, band);
        }

        if (detail::controls(theme).list_item_subtle_selection)
        {
            // A disabled row has no background, selected or not.
            const Color bg = enabled ? detail::subtle_row_background(theme, state, selected) : colors::Transparent;
            if (bg.a > 0.0f)
            {
                renderer.fill_rounded_rect(bounds(), theme.corner_radius, bg);
            }
            if (selected && !detail::controls(theme).row_selection_fills)
            {
                // A selected row gets an accent border instead of ListView's pill. Skipped under
                // `row_selection_fills`, where the row is already filled accent. A disabled grid
                // keeps it in the disabled text color, so the selection stays legible.
                stroke_rounded_rect_inside(renderer,
                                           bounds(),
                                           theme.corner_radius,
                                           enabled ? theme.colors().accent : theme.palette.text_fill_disabled,
                                           SelectedBorderThickness);
            }
        }
        else if (!enabled)
        {
            // A selected row in a disabled high-contrast grid: the window color behind GrayText, with the
            // selection kept as a GrayText indicator rather than a Highlight fill.
            if (selected)
            {
                stroke_rounded_rect_inside(renderer, bounds(), 0.0f, theme.colors().text_disabled, SelectedBorderThickness);
            }
        }
        else if (selected)
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, theme.colors().selection);
        }
        else if (hovered_)
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, theme.colors().control_fill_hover.with_alpha(TableViewHoverOverlayAlpha));
        }

        // The row's text: every enabled state is the primary text color where the
        // row is never filled, HighlightText on a Highlight-filled selection, and the disabled
        // text color on a disabled grid, selected or not.
        const Color text_color = !enabled ? theme.colors().text_disabled
                                 : detail::controls(theme).list_item_subtle_selection
                                     ? theme.colors().text
                                     : (selected ? theme.colors().selection_text : theme.colors().text);
        const auto& cells      = cells_of(row);

        const Thickness padding    = cell_padding(theme);
        const bool      wrapped    = owner_->size_rows_to_content_;
        const TextStyle style      = cell_text_style(theme, wrapped);
        const bool      vertical   = owner_->wants_vertical_lines();
        const Color     line_color = grid_line_color(theme);
        // The trimmed text from measure() while it is still this row's; the raw cell otherwise.
        const bool                           trimmed  = display_row_ == row;
        const TableViewControl::RealizedRow* realized = owner_->realized(row);
        const bool                           editing  = this->editing();

        // The one place a cell is drawn: its editor, its template Element or its text, then its
        // trailing grid line.
        const auto draw_cell = [&](std::size_t c)
        {
            const float    left  = bounds().left() + owner_->column_left(c);
            const float    width = owner_->column_actual_width(c);
            const Rect     cell{left, bounds().top(), width, bounds().height};
            const Element* content = editing && c == owner_->edit_column_                ? owner_->editor_.get()
                                     : realized != nullptr && c < realized->cells.size() ? realized->cells[c].get()
                                                                                         : nullptr;
            if (content != nullptr)
            {
                renderer.push_clip(cell);
                content->render(renderer, context);
                renderer.pop_clip();
            }
            else
            {
                const std::string& text = trimmed && c < display_.size() ? display_[c]
                                                                         : (c < cells.size() ? cells[c] : EmptyText);
                // Wrapped text keeps the cell padding top and bottom too.
                renderer.draw_text(text,
                                   Rect{left + padding.left,
                                        bounds().top() + (wrapped ? padding.top : 0.0f),
                                        std::max(0.0f, width - padding.horizontal()),
                                        std::max(0.0f, bounds().height - (wrapped ? padding.vertical() : 0.0f))},
                                   text_color,
                                   style);
            }
            if (vertical)
            {
                // The vertical grid line: the cell's trailing edge.
                renderer.stroke_line(Point{left + width, bounds().top()},
                                     Point{left + width, bounds().bottom()},
                                     line_color,
                                     1.0f);
            }
        };

        owner_->for_each_column_in_paint_order(renderer, bounds().top(), bounds().height, draw_cell);

        if (owner_->wants_horizontal_lines())
        {
            // The grid line color and thickness: the row separator, bottom edge only.
            renderer.stroke_line(Point{bounds().left(), bounds().bottom()},
                                 Point{bounds().right(), bounds().bottom()},
                                 line_color,
                                 1.0f);
        }
    }

    bool pointer_override(const PointerEvent& e) override
    {
        bool handled = false;
        switch (e.action)
        {
            case PointerAction::Move:
                if (const bool inside = bounds().contains(e.position); inside != hovered_)
                {
                    hovered_ = inside;
                    mark_dirty(DirtyFlags::Visual);
                }
                break;
            case PointerAction::Down:
                if (e.button == PointerButton::Left && bounds().contains(e.position))
                {
                    pressed_ = true;
                    hovered_ = true;
                    handled  = true;
                    mark_dirty(DirtyFlags::Visual);
                    shift_ = e.modifiers.shift;
                    ctrl_  = e.modifiers.ctrl;
                    if (!header())
                    {
                        owner_->cell_pressed(position_, e.position.x, e.click_count);
                    }
                }
                break;
            case PointerAction::Up:
                if (e.button == PointerButton::Left)
                {
                    const bool clicked = pressed_ && bounds().contains(e.position);
                    handled            = pressed_;
                    if (pressed_)
                    {
                        pressed_ = false;
                        mark_dirty(DirtyFlags::Visual);
                    }
                    if (clicked)
                    {
                        // The click is the last thing this row does: either call can rebuild the
                        // rows, this one included.
                        if (header())
                        {
                            owner_->group_header_clicked(position_);
                        }
                        else
                        {
                            owner_->row_clicked(position_, shift_, ctrl_);
                        }
                        return true;
                    }
                }
                break;
            case PointerAction::Leave:
                if (hovered_ || pressed_)
                {
                    hovered_ = false;
                    pressed_ = false;
                    mark_dirty(DirtyFlags::Visual);
                }
                break;
            default:
                break;
        }
        return handled;
    }

    // The row's template cells and its editor are the table's children, drawn over the row.
    void hit_test_children(Point /*p*/, std::vector<Element*>& out) const override
    {
        if (header())
        {
            return;
        }
        if (editing())
        {
            out.push_back(owner_->editor_.get());
        }
        if (const TableViewControl::RealizedRow* realized = owner_->realized(row()))
        {
            for (auto it = realized->cells.rbegin(); it != realized->cells.rend(); ++it)
            {
                if (*it)
                {
                    out.push_back(it->get());
                }
            }
        }
    }

    // A realized row is always measured before it is arranged (VirtualList::reflow()), so this is
    // where the trimmed cell text is worked out; render() has no measurer.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        // An unbounded height asks how tall the content is (the table sizes its rows to it).
        const bool to_content = !std::isfinite(available.height);
        Size       size{std::isfinite(available.width) ? available.width : 0.0f,
                        to_content ? owner_->row_height_ : available.height};
        if (header())
        {
            measure_group_header(size.width, context);
            if (Element* content = owner_->realize_group_header(owner_->position_group(position_)))
            {
                (void)content->measure(Size{header_content_width(size.width), size.height}, context);
            }
            return size;
        }
        const auto&     columns = owner_->columns_;
        const auto&     cells   = cells_of(row());
        const Thickness padding = cell_padding(context.theme);
        const TextStyle style   = cell_text_style(context.theme, to_content);
        display_.clear();
        display_.reserve(columns.size());
        for (std::size_t c = 0; c < columns.size(); ++c)
        {
            const std::string& text = c < cells.size() ? cells[c] : EmptyText;
            const float        room = std::max(0.0f, owner_->column_actual_width(c) - padding.horizontal());
            if (to_content)
            {
                display_.push_back(text);
                size.height = std::max(size.height,
                                       context.measurer.measure_wrapped(text, style, room).height + padding.vertical());
            }
            else
            {
                display_.push_back(trim_to_width(text, style, room, context.measurer));
            }
        }
        display_row_ = row();

        // The template cells and the editor take the whole cell, padding and all.
        const float cell_height = to_content ? Unbounded : size.height;
        const auto  cell_size   = [&](std::size_t c) { return Size{owner_->column_actual_width(c), cell_height}; };
        if (const TableViewControl::RealizedRow* realized = owner_->realize_cells(row()))
        {
            for (std::size_t c = 0; c < realized->cells.size(); ++c)
            {
                if (realized->cells[c])
                {
                    size.height = std::max(size.height, realized->cells[c]->measure(cell_size(c), context).height);
                }
            }
        }
        if (editing())
        {
            size.height = std::max(size.height, owner_->editor_->measure(cell_size(owner_->edit_column_), context).height);
        }
        return to_content ? size : Size{size.width, available.height};
    }

    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        if (header())
        {
            if (Element* content = owner_->group_header_element(owner_->position_group(position_)))
            {
                content->arrange(Rect{header_content_left(final_rect.left()),
                                      final_rect.top(),
                                      header_content_width(final_rect.width),
                                      final_rect.height});
            }
            return;
        }
        const auto cell_rect = [&](std::size_t c)
        {
            return Rect{final_rect.left() + owner_->column_left(c),
                        final_rect.top(),
                        owner_->column_actual_width(c),
                        final_rect.height};
        };
        if (TableViewControl::RealizedRow* realized = owner_->realized(row()))
        {
            for (std::size_t c = 0; c < realized->cells.size(); ++c)
            {
                if (realized->cells[c])
                {
                    realized->cells[c]->arrange(cell_rect(c));
                }
            }
        }
        if (editing())
        {
            owner_->editor_->arrange(cell_rect(owner_->edit_column_));
        }
        for (std::size_t c = 0; c < cell_automation_.size(); ++c)
        {
            cell_automation_[c]->place(cell_rect(c));
        }
    }


private:
    [[nodiscard]] std::size_t row() const
    {
        return owner_->row_at(position_);
    }
    [[nodiscard]] bool header() const
    {
        return owner_->is_group_header(position_);
    }
    [[nodiscard]] bool editing() const
    {
        return owner_->editor_ != nullptr && owner_->edit_row_ == row() && owner_->edit_column_ < owner_->columns_.size();
    }
    [[nodiscard]] const std::vector<std::string>& cells_of(std::size_t row) const
    {
        return row < owner_->rows_.size() ? owner_->rows_[row] : EmptyRow;
    }

    void measure_group_header(float width, const MeasureContext& context) const
    {
        const std::size_t group = owner_->position_group(position_);
        group_count_            = group_count_text(owner_->group_size(group));
        count_width_          = std::ceil(context.measurer.measure(group_count_, cell_text_style(context.theme)).width);
        const float     room  = std::max(0.0f,
                                         width - GroupHeaderPadding.horizontal() - GroupExpanderSize -
                                             2.0f * GroupHeaderSpacing - count_width_);
        const TextStyle style = group_key_style(context.theme);
        group_label_          = trim_to_width(owner_->group_key(group), style, room, context.measurer);
        label_width_          = std::min(room, std::ceil(context.measurer.measure(group_label_, style).width));
        measured_group_       = group;
    }

    void render_group_header(Renderer& renderer, const Theme& theme) const
    {
        const std::size_t group    = owner_->position_group(position_);
        const bool        enabled  = owner_->is_enabled();
        const bool        subtle   = detail::controls(theme).list_item_subtle_selection;
        const bool        expanded = owner_->is_group_expanded(group);

        // The group header band; hover and press swap it for the row's
        // own state fill. The era packs band it in their menu surface, like the column headers.
        const Color band = subtle ? (theme.dark ? GroupHeaderBackgroundDark : GroupHeaderBackgroundLight)
                                  : theme.surfaces.menu;
        if (enabled && subtle && (hovered_ || pressed_))
        {
            renderer.fill_rounded_rect(bounds(),
                                       0.0f,
                                       detail::subtle_row_background(theme,
                                                                     pressed_ ? ButtonState::Pressed : ButtonState::Hover,
                                                                     false));
        }
        else
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, band);
            if (enabled && hovered_)
            {
                renderer.fill_rounded_rect(bounds(),
                                           0.0f,
                                           theme.colors().control_fill_hover.with_alpha(TableViewHoverOverlayAlpha));
            }
        }

        // The group header text color, or the row's disabled one.
        const Color text = !enabled ? theme.colors().text_disabled
                           : subtle ? theme.palette.text_fill_primary
                                    : theme.surfaces.menu_text;
        const float left = bounds().left() + GroupHeaderPadding.left;
        draw_disclosure(renderer,
                        theme,
                        expanded ? GroupExpandedGlyph : GroupCollapsedGlyph,
                        expanded ? ArrowDirection::Down : ArrowDirection::Right,
                        Rect{left, bounds().top(), GroupExpanderSize, bounds().height},
                        text,
                        GroupChevronGlyphSize);

        const bool  measured = measured_group_ == group;
        const float key_left = left + GroupExpanderSize + GroupHeaderSpacing;
        if (const Element* content = owner_->group_header_element(group))
        {
            renderer.push_clip(bounds());
            content->render(renderer, theme);
            renderer.pop_clip();
        }
        else
        {
            draw_group_label(renderer, theme, text, key_left, measured);
        }

        if (owner_->wants_horizontal_lines())
        {
            renderer.stroke_line(Point{bounds().left(), bounds().bottom()},
                                 Point{bounds().right(), bounds().bottom()},
                                 grid_line_color(theme),
                                 1.0f);
        }
    }

    // Where a group header's template content goes: after the chevron, to the trailing padding.
    [[nodiscard]] static float header_content_left(float row_left)
    {
        return row_left + GroupHeaderPadding.left + GroupExpanderSize + GroupHeaderSpacing;
    }
    [[nodiscard]] static float header_content_width(float row_width)
    {
        return std::max(0.0f, row_width - GroupHeaderPadding.horizontal() - GroupExpanderSize - GroupHeaderSpacing);
    }

    void draw_group_label(Renderer& renderer, const Theme& theme, Color text, float key_left, bool measured) const
    {
        const std::size_t group = owner_->position_group(position_);
        renderer.draw_text(measured ? group_label_ : owner_->group_key(group),
                           Rect{key_left, bounds().top(), measured ? label_width_ : 0.0f, bounds().height},
                           text,
                           group_key_style(theme));
        if (measured)
        {
            renderer.draw_text(group_count_,
                               Rect{key_left + label_width_ + GroupHeaderSpacing, bounds().top(), count_width_, bounds().height},
                               text.with_alpha(text.a * GroupCountOpacity),
                               cell_text_style(theme));
        }
    }

    static inline const std::vector<std::string> EmptyRow;

    TableViewControl*                owner_;
    std::size_t                      position_;
    bool                             hovered_ = false;
    bool                             pressed_ = false;
    bool                             shift_   = false;
    bool                             ctrl_    = false;
    mutable std::vector<std::string> display_; // each cell's text, trimmed to its column
    mutable std::size_t              display_row_ = detail::NoIndex;
    // A group header's key, trimmed, and its count, with their widths.
    mutable std::string group_label_;
    mutable std::string group_count_;
    mutable float       label_width_    = 0.0f;
    mutable float       count_width_    = 0.0f;
    mutable std::size_t measured_group_ = detail::NoIndex;
    // The cells' automation nodes, built when a client first walks this row.
    mutable std::vector<std::unique_ptr<TableViewCellAutomation>> cell_automation_;
};

TableViewControl::TableViewControl() : list_(std::make_unique<VirtualListControl>())
{
    // A wheel turn or a scroll bar drag in the body: the pinned header and the cells follow.
    list_->scrolled_by_user_ = [this] { after_vertical_scroll(); };
    set_parent_of(*list_, this);
    // The rows are the table's own automation children, so the list between is left out.
    list_->set_accessibility(Accessibility{.decorative = true});
    list_->set_item_factory(
        [this](std::size_t i) -> std::unique_ptr<Element> { return std::make_unique<TableViewRow>(this, i); });
    // VirtualList's own item height default (32) is not row_height_ (40).
    list_->set_item_height(row_height_);
}

TableViewControl::~TableViewControl() = default;

// ---- content ----

void TableViewControl::set_columns(std::vector<TableViewColumn> columns)
{
    (void)close_edit(false);
    columns_ = std::move(columns);
    header_text_.clear(); // trimmed again by the next arrange()
    natural_widths_.clear();
    actual_widths_.clear();
    column_lefts_.clear();
    sort_column_    = detail::NoIndex;
    sort_direction_ = TableViewSortDirection::None;
    resizing_       = detail::NoIndex;
    pressed_header_ = detail::NoIndex;
    header_cursor_  = detail::NoIndex;
    current_column_ = detail::NoIndex;
    clear_cells();
    apply_sort();
    refresh_rows();
}

void TableViewControl::set_rows(std::vector<std::vector<std::string>> rows)
{
    replace_rows(std::move(rows), {});
}

void TableViewControl::replace_rows(std::vector<std::vector<std::string>> rows, std::vector<std::size_t> selected)
{
    (void)close_edit(false);
    clear_cells();
    rows_ = std::move(rows);
    apply_sort();
    std::erase_if(selected, [this](std::size_t row) { return row >= rows_.size(); });
    std::ranges::sort(selected);
    selected.erase(std::ranges::unique(selected).begin(), selected.end());
    if (selection_mode_ == SelectionMode::None)
    {
        selected.clear();
    }
    else if (selection_mode_ == SelectionMode::Single && selected.size() > 1)
    {
        selected.resize(1);
    }
    const bool changed = selected != selection_;
    selection_         = std::move(selected);
    cursor_            = selection_.empty() ? detail::NoIndex : selection_.front();
    anchor_            = cursor_;
    cursor_header_     = detail::NoIndex;
    list_->set_item_count(order_.size());
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (changed && on_selection_changed_)
    {
        on_selection_changed_(selected_row());
    }
}

void TableViewControl::set_size_rows_to_content(bool on)
{
    if (on == size_rows_to_content_)
    {
        return;
    }
    size_rows_to_content_ = on;
    list_->set_size_items_to_content(on);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::set_row_height(float height)
{
    row_height_ = height;
    list_->set_item_height(height);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::enabled_changed()
{
    if (!is_enabled())
    {
        (void)close_edit(false);
    }
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::focus_changed(bool focused)
{
    if (edit_target_ != nullptr)
    {
        set_focused_of(*edit_target_, focused);
    }
    // Focus leaving commits, once it has settled: a press elsewhere in the table hands focus
    // straight back, which must not close the edit it just opened.
    focus_commit_pending_ = !focused && editor_ != nullptr;
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::set_empty_content(std::unique_ptr<Element> content)
{
    empty_content_ = std::move(content);
    if (empty_content_)
    {
        set_parent_of(*empty_content_, this);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::refresh_rows()
{
    // The realized rows show other rows now: realize them again, trimmed to their columns.
    list_->set_item_count(order_.size());
    list_->set_scroll_offset(list_->scroll_offset());
    prune_cells();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- template cells ----

void TableViewControl::set_cell_factory(CellFactory factory)
{
    (void)close_edit(false);
    clear_cells();
    cell_factory_ = std::move(factory);
    refresh_rows();
}

TableViewControl::RealizedRow* TableViewControl::realized(std::size_t row) const
{
    for (RealizedRow& entry : realized_)
    {
        if (entry.row == row)
        {
            return &entry;
        }
    }
    return nullptr;
}

// Measure (for an Auto column) and the row's own measure realize the cells; the result is only
// good until the next call, which may grow realized_.
TableViewControl::RealizedRow* TableViewControl::realize_cells(std::size_t row) const
{
    if (!cell_factory_ || row >= rows_.size())
    {
        return nullptr;
    }
    if (RealizedRow* entry = realized(row))
    {
        return entry;
    }
    // Kept even when every cell is text, so the factory is asked once per row in view.
    RealizedRow entry;
    entry.row = row;
    entry.cells.resize(columns_.size());
    auto* self = const_cast<TableViewControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): realized_ is a cache
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        entry.cells[c] = cell_factory_(row, c);
        if (entry.cells[c])
        {
            set_parent_of(*entry.cells[c], self);
        }
    }
    realized_.push_back(std::move(entry));
    return &realized_.back();
}

void TableViewControl::set_group_header_factory(GroupHeaderFactory factory)
{
    group_header_factory_ = std::move(factory);
    retire_group_headers();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

Element* TableViewControl::group_header_element(std::size_t group) const
{
    for (const auto& [key, content] : realized_headers_)
    {
        if (key == group)
        {
            return content.get();
        }
    }
    return nullptr;
}

Element* TableViewControl::realize_group_header(std::size_t group) const
{
    if (!group_header_factory_ || group >= groups_.size())
    {
        return nullptr;
    }
    if (Element* content = group_header_element(group))
    {
        return content;
    }
    std::unique_ptr<Element> content = group_header_factory_(group);
    if (!content)
    {
        return nullptr;
    }
    set_parent_of(*content, const_cast<TableViewControl*>(this)); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    realized_headers_.emplace_back(group, std::move(content));
    return realized_headers_.back().second.get();
}

void TableViewControl::retire_group_headers()
{
    for (auto& entry : realized_headers_)
    {
        retired_.push_back(std::move(entry.second));
    }
    realized_headers_.clear();
}

Element* TableViewControl::cell_element(std::size_t row, std::size_t column) const
{
    const RealizedRow* entry = realized(row);
    return entry != nullptr && column < entry->cells.size() ? entry->cells[column].get() : nullptr;
}

// Drops the cells of the rows that left the view; a cell holding focus stays, as a focused element
// stays realized in a ForEach.
void TableViewControl::prune_cells()
{
    const std::size_t first = list_->first_visible_index();
    const std::size_t last  = first + list_->visible_count();
    std::erase_if(realized_headers_,
                  [&](auto& entry)
                  {
                      const std::size_t position = entry.first < groups_.size() ? groups_[entry.first].header : detail::NoIndex;
                      if ((position != detail::NoIndex && position >= first && position < last) ||
                          entry.second->has_focus_within())
                      {
                          return false;
                      }
                      retired_.push_back(std::move(entry.second));
                      return true;
                  });
    if (realized_.empty())
    {
        return;
    }
    std::erase_if(realized_,
                  [&](RealizedRow& entry)
                  {
                      const std::size_t position = row_position(entry.row);
                      if (position != detail::NoIndex && position >= first && position < last)
                      {
                          return false;
                      }
                      if (std::ranges::any_of(entry.cells,
                                              [](const std::unique_ptr<Element>& cell)
                                              { return cell && cell->has_focus_within(); }))
                      {
                          return false;
                      }
                      for (std::unique_ptr<Element>& cell : entry.cells)
                      {
                          if (cell)
                          {
                              retired_.push_back(std::move(cell));
                          }
                      }
                      return true;
                  });
}

void TableViewControl::clear_cells()
{
    retire_group_headers();
    for (RealizedRow& entry : realized_)
    {
        for (std::unique_ptr<Element>& cell : entry.cells)
        {
            if (cell)
            {
                retired_.push_back(std::move(cell));
            }
        }
    }
    realized_.clear();
}

// ---- editing ----

void TableViewControl::set_read_only(bool read_only)
{
    if (read_only_ == read_only)
    {
        return;
    }
    read_only_ = read_only;
    if (read_only && !commit_edit())
    {
        (void)cancel_edit();
    }
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::set_cell_editor_factory(CellFactory factory)
{
    editor_factory_ = std::move(factory);
}

bool TableViewControl::column_editable(std::size_t column) const
{
    return !read_only_ && column < columns_.size() && !columns_[column].read_only;
}

std::size_t TableViewControl::first_editable_column() const
{
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        if (column_editable(c))
        {
            return c;
        }
    }
    return detail::NoIndex;
}

const std::string& TableViewControl::cell_text(std::size_t row, std::size_t column) const
{
    return row < rows_.size() && column < rows_[row].size() ? rows_[row][column] : EmptyText;
}

void TableViewControl::set_cell_text(std::size_t row, std::size_t column, std::string text)
{
    if (row >= rows_.size())
    {
        return;
    }
    if (rows_[row].size() <= column)
    {
        rows_[row].resize(column + 1);
    }
    rows_[row][column] = std::move(text);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); // the row trims its text in measure()
}

void TableViewControl::update_row(std::size_t row, std::vector<std::string> cells, std::optional<std::string> group_key)
{
    if (row >= rows_.size())
    {
        return;
    }
    rows_[row] = std::move(cells);
    // Its template cells were built from the item it had, so they are built again from this one.
    if (RealizedRow* entry = realized(row))
    {
        for (std::size_t c = 0; c < entry->cells.size(); ++c)
        {
            if (entry->cells[c])
            {
                retired_.push_back(std::move(entry->cells[c]));
                entry->cells[c] = cell_factory_ ? cell_factory_(row, c) : nullptr;
                if (entry->cells[c])
                {
                    set_parent_of(*entry->cells[c], this);
                }
            }
        }
    }
    if (group_key && row < group_keys_.size() && group_keys_[row] != *group_key)
    {
        group_keys_[row] = std::move(*group_key);
        apply_sort();
        refresh_rows();
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::set_current_column(std::size_t column)
{
    if (columns_.empty())
    {
        return;
    }
    current_column_ = std::min(column, columns_.size() - 1);
    scroll_column_into_view(current_column_);
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::scroll_column_into_view(std::size_t column)
{
    if (column >= column_lefts_.size() || column >= actual_widths_.size() || column_pinned(column))
    {
        return;
    }
    const float frozen   = frozen_width();
    const float trailing = trailing_frozen_width();
    const float left     = column_lefts_[column];
    const float right    = left + actual_widths_[column];
    float       offset   = horizontal_offset_;
    if (left < offset + frozen)
    {
        offset = left - frozen;
    }
    else if (right > offset + view_width() - trailing)
    {
        offset = right - view_width() + trailing;
    }
    set_horizontal_offset(offset);
}

std::size_t TableViewControl::column_at(float x) const
{
    const float frozen_edge   = scrolling_left();
    const float trailing_edge = scrolling_right();
    for (std::size_t c = 0; c < columns_.size() && c < actual_widths_.size(); ++c)
    {
        const float left = inner_rect().left() + column_left(c);
        if (!column_pinned(c) && (x < frozen_edge || x >= trailing_edge))
        {
            continue; // under a frozen band
        }
        if (x >= left && x < left + actual_widths_[c])
        {
            return c;
        }
    }
    return detail::NoIndex;
}

bool TableViewControl::begin_edit(std::size_t row, std::size_t column)
{
    if (!is_enabled() || row >= rows_.size() || row_position(row) == detail::NoIndex || !column_editable(column))
    {
        return false;
    }
    if (editor_ != nullptr)
    {
        if (row == edit_row_ && column == edit_column_)
        {
            return true;
        }
        if (!commit_edit())
        {
            return false;
        }
    }
    current_column_ = column;
    if (on_beginning_edit_ && detail::canceled(on_beginning_edit_(TableViewCellArgs{.row = row, .column = column})))
    {
        return false;
    }

    // The column's own editor first, as an editing cell wins over the text column's TextBox;
    // a template cell has no text to fall back to, so without one it is not editable.
    std::unique_ptr<Element> editor = editor_factory_ ? editor_factory_(row, column) : nullptr;
    TextBoxControl*          box    = nullptr;
    if (!editor)
    {
        const RealizedRow* cells = realize_cells(row);
        if (cells != nullptr && column < cells->cells.size() && cells->cells[column])
        {
            return false;
        }
        // Matches the display cell, so opening the editor neither moves the text nor grows the row.
        auto text_box = std::make_unique<TextBoxControl>(cell_text(row, column));
        text_box->set_uses_theme_min_height(false);
        text_box->set_font_weight(FontWeight::Regular);
        box    = text_box.get();
        editor = std::move(text_box);
    }
    set_parent_of(*editor, this);
    editor->set_vertical_alignment(Alignment::Center);
    editor_      = std::move(editor);
    edit_box_    = box;
    edit_row_    = row;
    edit_column_ = column;
    std::vector<Element*> stops;
    editor_->collect_focusables(stops);
    edit_target_ = stops.empty() ? nullptr : stops.front();
    if (box != nullptr)
    {
        // Select all, as the platform grid does, so the first keystroke replaces the value.
        set_focused_of(*box, true);
        box->handle_key(KeyEvent{.key = Key::A, .action = KeyAction::Down, .modifiers = {.ctrl = true}});
        set_focused_of(*box, is_focused());
    }
    else if (edit_target_ != nullptr && is_focused())
    {
        set_focused_of(*edit_target_, true);
    }
    cursor_        = row;
    cursor_header_ = detail::NoIndex;
    scroll_into_view(row_position(row));
    scroll_column_into_view(column);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return true;
}

bool TableViewControl::commit_edit()
{
    return close_edit(true);
}

bool TableViewControl::cancel_edit()
{
    return close_edit(false);
}

bool TableViewControl::close_edit(bool commit)
{
    if (editor_ == nullptr)
    {
        return false;
    }
    const std::size_t row    = edit_row_;
    const std::size_t column = edit_column_;
    std::string       text;
    if (commit)
    {
        text = edit_box_ != nullptr ? edit_box_->text() : cell_text(row, column);
        if (on_cell_edited_ &&
            detail::canceled(on_cell_edited_(TableViewCellEditArgs{.row = row, .column = column, .text = text})))
        {
            return false; // rejected: the editor stays open
        }
        if (editor_ == nullptr || edit_row_ != row || edit_column_ != column)
        {
            return true; // the handler closed it itself (replaced the rows, say)
        }
    }
    // The table holds focus throughout, so the editor is never Window's focused element; a popup it
    // opened may still be listed, though, so it goes with the next layout.
    if (edit_target_ != nullptr)
    {
        set_focused_of(*edit_target_, false);
    }
    retired_.push_back(std::move(editor_));
    edit_target_          = nullptr;
    edit_box_             = nullptr;
    edit_row_             = detail::NoIndex;
    edit_column_          = detail::NoIndex;
    focus_commit_pending_ = false;
    if (commit)
    {
        set_cell_text(row, column, std::move(text));
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return true;
}

void TableViewControl::cell_pressed(std::size_t position, float x, int click_count)
{
    if (!is_enabled())
    {
        return;
    }
    const std::size_t row    = row_at(position);
    const std::size_t column = column_at(x);
    if (row == detail::NoIndex || column == detail::NoIndex)
    {
        return;
    }
    // A press in the open editor is the user placing the caret, not a move.
    if (editor_ != nullptr && row == edit_row_ && column == edit_column_)
    {
        return;
    }
    // The current cell moves even when the edit is refused, so a later F2 edits the cell pointed at.
    current_column_ = column;
    mark_dirty(DirtyFlags::Visual);
    if (click_count >= 2)
    {
        // The press bubbles on up to this table, which must not take it for one outside the edit.
        opened_by_press_ = begin_edit(row, column);
    }
}

// ---- grouping ----

void TableViewControl::set_group_keys(std::vector<std::string> keys)
{
    group_keys_    = std::move(keys);
    cursor_header_ = detail::NoIndex;
    apply_sort();
    refresh_rows();
}

void TableViewControl::set_sticky_group_headers(bool sticky)
{
    if (sticky_headers_ == sticky)
    {
        return;
    }
    sticky_headers_ = sticky;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::update_sticky_header()
{
    sticky_rect_ = Rect{};
    if (!sticky_headers_ || groups_.empty() || measurer_ == nullptr || theme_ == nullptr || order_.empty())
    {
        sticky_row_.reset();
        sticky_position_ = detail::NoIndex;
        return;
    }
    const Rect        body   = list_->viewport_rect();
    const float       offset = list_->scroll_offset();
    const std::size_t top    = std::min(list_->index_at(offset), order_.size() - 1);
    const std::size_t group  = position_group(top);
    if (group >= groups_.size() || groups_[group].header >= order_.size())
    {
        sticky_row_.reset();
        sticky_position_ = detail::NoIndex;
        return;
    }
    const std::size_t header = groups_[group].header;
    const float       height = list_->item_extent(header);
    if (body.top() + list_->item_top(header) - offset >= body.top())
    {
        return; // the header itself is at the top
    }
    // Pinned at the top until the next group's header pushes it up.
    float y = body.top();
    if (group + 1 < groups_.size() && groups_[group + 1].header < order_.size())
    {
        y = std::min(y, body.top() + list_->item_top(groups_[group + 1].header) - offset - height);
    }
    if (!sticky_row_ || sticky_position_ != header)
    {
        sticky_row_ = std::make_unique<TableViewRow>(this, header);
        set_parent_of(*sticky_row_, this);
        sticky_position_ = header;
    }
    (void)sticky_row_->measure(Size{body.width, height}, *measurer_, *theme_);
    sticky_rect_ = Rect{body.left(), y, body.width, height};
    sticky_row_->arrange(sticky_rect_);
}

void TableViewControl::set_grouping_enabled(bool enabled)
{
    if (grouping_enabled_ == enabled)
    {
        return;
    }
    grouping_enabled_ = enabled;
    cursor_header_    = detail::NoIndex;
    apply_sort();
    refresh_rows();
}

const std::string& TableViewControl::group_key(std::size_t group) const
{
    return group < groups_.size() ? groups_[group].key : EmptyText;
}

void TableViewControl::set_group_expanded(std::size_t group, bool expanded)
{
    if (group >= groups_.size() || groups_[group].expanded == expanded)
    {
        return;
    }
    const std::string key = groups_[group].key;
    if (expanded)
    {
        std::erase(collapsed_, key);
    }
    else
    {
        collapsed_.push_back(key);
        // An edit over a row about to be hidden closes first.
        if (editor_ != nullptr && row_group(edit_row_) == group && !commit_edit())
        {
            (void)cancel_edit();
        }
        const std::size_t keyed = selection_mode_ == SelectionMode::Single && first_selected_row() != detail::NoIndex
                                      ? first_selected_row()
                                      : cursor_;
        if (cursor_header_ == detail::NoIndex && keyed != detail::NoIndex && row_group(keyed) == group)
        {
            cursor_header_ = group;
        }
    }
    apply_sort();
    refresh_rows();
}

void TableViewControl::expand_all_groups()
{
    if (collapsed_.empty())
    {
        return;
    }
    collapsed_.clear();
    apply_sort();
    refresh_rows();
}

void TableViewControl::collapse_all_groups()
{
    for (std::size_t g = 0; g < groups_.size(); ++g)
    {
        set_group_expanded(g, false);
    }
}

void TableViewControl::toggle_group(std::size_t group)
{
    set_group_expanded(group, !is_group_expanded(group));
}

void TableViewControl::group_header_clicked(std::size_t position)
{
    const std::size_t group = position_group(position);
    if (!is_enabled() || group == detail::NoIndex)
    {
        return;
    }
    if (editor_ != nullptr && !commit_edit())
    {
        return;
    }
    cursor_header_ = group;
    toggle_group(group);
}

// ---- columns ----

void TableViewControl::set_column_width(std::size_t column, TableViewColumnWidth width)
{
    if (column >= columns_.size() || columns_[column].width == width)
    {
        return;
    }
    columns_[column].width = width;
    if (size_rows_to_content_)
    {
        list_->forget_item_heights(); // the text rewraps in the new width
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::set_column_frozen(std::size_t column, bool frozen)
{
    if (column >= columns_.size() || columns_[column].frozen == frozen)
    {
        return;
    }
    columns_[column].frozen = frozen;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); // the template cells move with it
}

void TableViewControl::set_column_frozen_trailing(std::size_t column, bool frozen)
{
    if (column >= columns_.size() || columns_[column].frozen_trailing == frozen)
    {
        return;
    }
    columns_[column].frozen_trailing = frozen;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TableViewControl::set_can_user_resize_columns(bool can)
{
    can_user_resize_columns_ = can;
    if (!can)
    {
        resizing_ = detail::NoIndex;
    }
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::set_horizontal_offset(float offset)
{
    offset = std::clamp(offset, 0.0f, horizontal_extent());
    if (offset == horizontal_offset_)
    {
        return;
    }
    horizontal_offset_ = offset;
    hbar_.notify_activity();
    // Layout too while there are template cells, an editor or automation nodes: they are arranged
    // at their columns.
    mark_dirty(realized_.empty() && editor_ == nullptr && !header_automation_
                   ? DirtyFlags::Visual
                   : DirtyFlags::Layout | DirtyFlags::Visual);
}

float TableViewControl::content_width() const
{
    return std::accumulate(actual_widths_.begin(), actual_widths_.end(), 0.0f);
}

float TableViewControl::horizontal_extent() const
{
    return std::max(0.0f, content_width() - view_width());
}

float TableViewControl::view_width() const
{
    return std::max(0.0f, inner_rect().width - list_->right_gutter());
}

Rect TableViewControl::hbar_viewport() const
{
    const Rect body = body_rect();
    return Rect{body.left(), body.top(), view_width(), body.height};
}

bool TableViewControl::hbar_shown() const
{
    return hbar_.gutter() > 0.0f ? horizontal_gutter_ > 0.0f : horizontal_extent() > 0.0f;
}

bool TableViewControl::column_frozen(std::size_t column) const
{
    // Only the leading run of frozen columns is pinned.
    for (std::size_t c = 0; c <= column && c < columns_.size(); ++c)
    {
        if (!columns_[c].frozen)
        {
            return false;
        }
    }
    return column < columns_.size();
}

float TableViewControl::frozen_width() const
{
    float width = 0.0f;
    for (std::size_t c = 0; c < columns_.size() && columns_[c].frozen && c < actual_widths_.size(); ++c)
    {
        width += actual_widths_[c];
    }
    return width;
}

bool TableViewControl::column_frozen_trailing(std::size_t column) const
{
    // Only the trailing run is pinned, and a column of the leading run stays there.
    if (column >= columns_.size() || column_frozen(column))
    {
        return false;
    }
    for (std::size_t c = column; c < columns_.size(); ++c)
    {
        if (!columns_[c].frozen_trailing)
        {
            return false;
        }
    }
    return true;
}

float TableViewControl::trailing_frozen_width() const
{
    float width = 0.0f;
    for (std::size_t c = 0; c < columns_.size() && c < actual_widths_.size(); ++c)
    {
        if (column_frozen_trailing(c))
        {
            width += actual_widths_[c];
        }
    }
    return width;
}

float TableViewControl::scrolling_left() const
{
    return inner_rect().left() + frozen_width();
}

float TableViewControl::scrolling_right() const
{
    // Without a trailing band the scrolling columns run to the bounds, under the vertical gutter;
    // with one they stop where it is pinned.
    const float trailing = trailing_frozen_width();
    return trailing > 0.0f ? inner_rect().left() + view_width() - trailing : inner_rect().right();
}

template <typename Fn>
void TableViewControl::for_each_column_in_paint_order(Renderer& renderer, float top, float height, Fn&& draw) const
{
    // While the columns overflow, the scrolling ones are clipped to the band between the frozen
    // ones, which are then drawn over them without moving.
    const bool clip = horizontal_extent() > 0.0f;
    if (clip)
    {
        const float left = scrolling_left();
        renderer.push_clip(Rect{left, top, std::max(0.0f, scrolling_right() - left), height});
    }
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        if (!column_pinned(c))
        {
            draw(c);
        }
    }
    if (clip)
    {
        renderer.pop_clip();
    }
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        if (column_pinned(c))
        {
            draw(c);
        }
    }
}

float TableViewControl::column_left(std::size_t column) const
{
    if (column >= column_lefts_.size())
    {
        return 0.0f;
    }
    if (column_frozen(column))
    {
        return column_lefts_[column];
    }
    // A trailing frozen column sits where it does with the columns scrolled to their end.
    return column_lefts_[column] - (column_frozen_trailing(column) ? horizontal_extent() : horizontal_offset_);
}

Rect TableViewControl::inner_rect() const
{
    return bounds().inset(Thickness{BorderThickness});
}

Rect TableViewControl::body_rect() const
{
    return Rect{inner_rect().left(),
                inner_rect().top() + header_height_,
                inner_rect().width,
                std::max(0.0f, inner_rect().height - header_height_)};
}

float TableViewControl::natural_width(std::size_t column) const
{
    return column < natural_widths_.size() ? natural_widths_[column] : 0.0f;
}

// Fixed and Auto columns first, then the fraction columns split what is left by weight; a
// fraction column that its min or max clamps keeps that width and leaves the rest to the others.
// On an unbounded axis a fraction column sizes like Auto.
void TableViewControl::resolve_columns(float viewport_width) const
{
    const std::size_t n = columns_.size();
    actual_widths_.assign(n, 0.0f);
    column_lefts_.assign(n, 0.0f);
    std::vector<bool> pending(n, false);
    float             fixed = 0.0f;
    for (std::size_t c = 0; c < n; ++c)
    {
        const TableViewColumn& column = columns_[c];
        switch (column.width.unit)
        {
            case GridUnit::Dip:
                actual_widths_[c] = clamp_width(column.width.value, column);
                break;
            case GridUnit::Auto:
                actual_widths_[c] = clamp_width(natural_width(c), column);
                break;
            case GridUnit::Fraction:
                if (std::isfinite(viewport_width))
                {
                    pending[c] = true;
                    continue;
                }
                actual_widths_[c] = clamp_width(natural_width(c), column);
                break;
        }
        fixed += actual_widths_[c];
    }

    float remaining = std::max(0.0f, viewport_width - fixed);
    for (bool clamped = true; clamped;)
    {
        clamped      = false;
        float weight = 0.0f;
        for (std::size_t c = 0; c < n; ++c)
        {
            if (pending[c])
            {
                weight += std::max(0.0f, columns_[c].width.value);
            }
        }
        for (std::size_t c = 0; c < n && !clamped; ++c)
        {
            if (!pending[c])
            {
                continue;
            }
            const float share = weight > 0.0f ? remaining * std::max(0.0f, columns_[c].width.value) / weight : 0.0f;
            const float clamped_share = clamp_width(share, columns_[c]);
            actual_widths_[c]         = clamped_share;
            if (clamped_share != share)
            {
                // Fixed at its bound; the others share what is left.
                pending[c] = false;
                remaining  = std::max(0.0f, remaining - clamped_share);
                clamped    = true;
            }
        }
    }

    float left = 0.0f;
    for (std::size_t c = 0; c < n; ++c)
    {
        column_lefts_[c] = left;
        left += actual_widths_[c];
    }
}

std::string TableViewControl::tool_tip_at(Point p) const
{
    if (cell_tool_tip_ && row_height_ > 0.0f && header_at(p) == detail::NoIndex)
    {
        const Rect body = list_->viewport_rect();
        if (body.contains(p))
        {
            const std::size_t position = list_->index_at(p.y - body.top() + list_->scroll_offset());
            const std::size_t column   = column_at(p.x);
            const std::size_t row      = row_at(position);
            if (row != detail::NoIndex && column != detail::NoIndex)
            {
                if (std::string tip = cell_tool_tip_(row, column); !tip.empty())
                {
                    return tip;
                }
            }
        }
    }
    return tool_tip();
}

std::size_t TableViewControl::header_at(Point p) const
{
    if (p.y < inner_rect().top() || p.y >= inner_rect().top() + header_height_ || p.x < inner_rect().left() ||
        p.x >= inner_rect().right())
    {
        return detail::NoIndex;
    }
    return column_at(p.x);
}

std::size_t TableViewControl::divider_at(Point p) const
{
    if (!can_user_resize_columns_ || !is_enabled())
    {
        return detail::NoIndex;
    }
    const std::size_t column = header_at(p);
    if (column == detail::NoIndex || !columns_[column].can_resize)
    {
        return detail::NoIndex;
    }
    const float right = inner_rect().left() + column_left(column) + actual_widths_[column];
    return p.x >= right - ResizeGripperWidth ? column : detail::NoIndex;
}

std::optional<CursorShape> TableViewControl::cursor_at(Point p) const
{
    if (resizing_ != detail::NoIndex || divider_at(p) != detail::NoIndex)
    {
        return CursorShape::SizeWestEast;
    }
    return std::nullopt;
}

// ---- sorting ----

std::weak_ordering TableViewControl::compare_text(const std::string& a, const std::string& b)
{
    const std::optional<double> na = as_number(a);
    const std::optional<double> nb = as_number(b);
    if (na && nb)
    {
        return *na < *nb   ? std::weak_ordering::less
               : *nb < *na ? std::weak_ordering::greater
                           : std::weak_ordering::equivalent;
    }
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const char ca = fold(a[i]);
        const char cb = fold(b[i]);
        if (ca != cb)
        {
            return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb)
                       ? std::weak_ordering::less
                       : std::weak_ordering::greater;
        }
    }
    if (a.size() != b.size())
    {
        return a.size() < b.size() ? std::weak_ordering::less : std::weak_ordering::greater;
    }
    return a <=> b; // "abc" before "ABC" only when nothing else tells them apart
}

// Builds the view: the rows in sort order and, while grouped, filed under a header row per group,
// the groups in the order their keys first appear and a collapsed group's rows left out.
void TableViewControl::apply_sort()
{
    std::vector<std::size_t> sorted(rows_.size());
    std::iota(sorted.begin(), sorted.end(), std::size_t{0});
    if (sort_column_ != detail::NoIndex && sort_direction_ != TableViewSortDirection::None &&
        sort_column_ < columns_.size())
    {
        const std::size_t column     = sort_column_;
        const bool        descending = sort_direction_ == TableViewSortDirection::Descending;
        const auto        cell       = [&](std::size_t row) -> const std::string& { return cell_text(row, column); };
        // Stable both ways, so rows that compare equal keep their source order.
        std::ranges::stable_sort(sorted,
                                 [&](std::size_t a, std::size_t b)
                                 {
                                     const std::weak_ordering o = sort_comparer_ ? sort_comparer_(column, a, b)
                                                                                 : compare_text(cell(a), cell(b));
                                     return descending ? o > 0 : o < 0;
                                 });
    }

    retire_group_headers(); // built for the groups that are about to go
    groups_.clear();
    row_group_.clear();
    position_group_.clear();
    if (!grouping_enabled_ || group_keys_.empty() || rows_.empty())
    {
        order_ = std::move(sorted);
    }
    else
    {
        std::unordered_map<std::string, std::size_t> index;
        row_group_.assign(rows_.size(), detail::NoIndex);
        for (std::size_t r = 0; r < rows_.size(); ++r)
        {
            const std::string& key = r < group_keys_.size() ? group_keys_[r] : EmptyText;
            const auto [it, added] = index.try_emplace(key, groups_.size());
            if (added)
            {
                groups_.push_back(Group{.key = key, .expanded = std::ranges::find(collapsed_, key) == collapsed_.end()});
            }
            row_group_[r] = it->second;
            ++groups_[it->second].count;
        }
        std::vector<std::vector<std::size_t>> members(groups_.size());
        for (const std::size_t r : sorted)
        {
            members[row_group_[r]].push_back(r);
        }
        order_.clear();
        order_.reserve(rows_.size() + groups_.size());
        for (std::size_t g = 0; g < groups_.size(); ++g)
        {
            groups_[g].header = order_.size();
            order_.push_back(detail::NoIndex);
            position_group_.push_back(g);
            if (groups_[g].expanded)
            {
                for (const std::size_t r : members[g])
                {
                    order_.push_back(r);
                    position_group_.push_back(g);
                }
            }
        }
    }
    positions_.assign(rows_.size(), detail::NoIndex);
    for (std::size_t position = 0; position < order_.size(); ++position)
    {
        if (order_[position] != detail::NoIndex)
        {
            positions_[order_[position]] = position;
        }
    }
    if (cursor_header_ >= groups_.size())
    {
        cursor_header_ = detail::NoIndex;
    }
}

bool TableViewControl::sort_by_column(std::size_t column, TableViewSortDirection direction)
{
    if (column >= columns_.size())
    {
        return false;
    }
    if (direction == TableViewSortDirection::None)
    {
        if (sort_column_ == detail::NoIndex)
        {
            return false;
        }
        sort_column_    = detail::NoIndex;
        sort_direction_ = TableViewSortDirection::None;
    }
    else
    {
        if (column == sort_column_ && direction == sort_direction_)
        {
            return false;
        }
        sort_column_    = column;
        sort_direction_ = direction;
    }
    apply_sort();
    refresh_rows();
    if (on_sorted_)
    {
        on_sorted_(TableViewSortedArgs{.column = detail::optional_index(sort_column_), .direction = sort_direction_});
    }
    return true;
}

bool TableViewControl::toggle_sort_direction(std::size_t column)
{
    if (column >= columns_.size())
    {
        return false;
    }
    const TableViewSortCycle     cycle            = columns_[column].sort_cycle;
    const bool                   opens_descending = cycle == TableViewSortCycle::DescendingAscending ||
                                                    cycle == TableViewSortCycle::DescendingAscendingNone;
    const bool                   ends_unsorted    = cycle == TableViewSortCycle::AscendingDescendingNone ||
                                                    cycle == TableViewSortCycle::DescendingAscendingNone;
    const TableViewSortDirection opening          = opens_descending ? TableViewSortDirection::Descending
                                                                     : TableViewSortDirection::Ascending;
    const TableViewSortDirection second           = opens_descending ? TableViewSortDirection::Ascending
                                                                     : TableViewSortDirection::Descending;
    const TableViewSortDirection current = column == sort_column_ ? sort_direction_ : TableViewSortDirection::None;

    TableViewSortDirection next = opening;
    if (current == opening)
    {
        next = second;
    }
    else if (current == second)
    {
        next = ends_unsorted ? TableViewSortDirection::None : opening;
    }
    if (on_sorting_)
    {
        TableViewSortingArgs args{.column = column, .direction = next};
        bool                 canceled = false;
        canceled                      = detail::canceled(on_sorting_(args));
        if (canceled)
        {
            return false;
        }
    }
    return sort_by_column(column, next);
}

void TableViewControl::set_can_user_sort_columns(bool can)
{
    can_user_sort_columns_ = can;
    pressed_header_        = detail::NoIndex;
    mark_dirty(DirtyFlags::Visual);
}

// ---- selection ----

bool TableViewControl::is_row_selected(std::size_t row) const
{
    return std::ranges::binary_search(selection_, row);
}

void TableViewControl::replace_selection(std::vector<std::size_t> rows)
{
    std::erase_if(rows, [this](std::size_t row) { return row >= rows_.size(); });
    std::ranges::sort(rows);
    rows.erase(std::ranges::unique(rows).begin(), rows.end());
    if (selection_mode_ == SelectionMode::None)
    {
        rows.clear();
    }
    else if (selection_mode_ == SelectionMode::Single && rows.size() > 1)
    {
        rows.resize(1);
    }
    if (rows == selection_)
    {
        return;
    }
    selection_ = std::move(rows);
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected_row());
    }
}

void TableViewControl::select_row(std::size_t index)
{
    if (rows_.empty() || selection_mode_ == SelectionMode::None)
    {
        return;
    }
    if (index == detail::NoIndex)
    {
        replace_selection({}); // None names no row; it is not "the last one"
        return;
    }
    index = std::min(index, rows_.size() - 1);
    replace_selection({index});
    cursor_ = anchor_ = index;
    cursor_header_    = detail::NoIndex;
    scroll_into_view(positions_[index]);
}

void TableViewControl::set_selected_rows(std::vector<std::size_t> rows)
{
    replace_selection(std::move(rows));
    if (!selection_.empty())
    {
        cursor_ = anchor_ = selection_.front();
    }
}

void TableViewControl::set_selection_mode(SelectionMode mode)
{
    if (mode == selection_mode_)
    {
        return;
    }
    selection_mode_ = mode;
    switch (mode)
    {
        case SelectionMode::None:
            replace_selection({});
            break;
        case SelectionMode::Single:
            if (selection_.size() > 1)
            {
                replace_selection({selection_.front()});
            }
            break;
        case SelectionMode::Multiple:
        case SelectionMode::Extended:
            break;
    }
    mark_dirty(DirtyFlags::Visual);
}

std::size_t TableViewControl::first_row_position() const
{
    for (std::size_t position = 0; position < order_.size(); ++position)
    {
        if (order_[position] != detail::NoIndex)
        {
            return position;
        }
    }
    return detail::NoIndex;
}

// Every row between two view positions; the group headers in between are skipped.
void TableViewControl::select_range(std::size_t from_position, std::size_t to_position, bool clear_first)
{
    const std::size_t        lo   = std::min(from_position, to_position);
    const std::size_t        hi   = std::min(std::max(from_position, to_position), order_.size() - 1);
    std::vector<std::size_t> rows = clear_first ? std::vector<std::size_t>{} : selection_;
    for (std::size_t p = lo; p <= hi; ++p)
    {
        if (order_[p] != detail::NoIndex)
        {
            rows.push_back(order_[p]);
        }
    }
    replace_selection(std::move(rows));
}

void TableViewControl::row_clicked(std::size_t position, bool shift, bool ctrl)
{
    if (!is_enabled() || position >= order_.size() || order_[position] == detail::NoIndex)
    {
        return;
    }
    const std::size_t row = order_[position];
    cursor_               = row;
    cursor_header_        = detail::NoIndex;
    mark_dirty(DirtyFlags::Visual);
    // An anchor in a collapsed group has no position; the click starts over from its own row.
    if (anchor_ == detail::NoIndex || row_position(anchor_) == detail::NoIndex)
    {
        anchor_ = row;
    }
    switch (selection_mode_)
    {
        case SelectionMode::None:
            return;
        case SelectionMode::Single:
            replace_selection({row});
            anchor_ = row;
            return;
        case SelectionMode::Multiple:
            if (shift)
            {
                // The range takes the anchor's state, as a list's Multiple mode does.
                const bool select = is_row_selected(anchor_);
                if (select)
                {
                    select_range(positions_[anchor_], position, false);
                }
                else
                {
                    const std::size_t        lo = std::min(positions_[anchor_], position);
                    const std::size_t        hi = std::max(positions_[anchor_], position);
                    std::vector<std::size_t> rows;
                    for (const std::size_t r : selection_)
                    {
                        if (positions_[r] == detail::NoIndex || positions_[r] < lo || positions_[r] > hi)
                        {
                            rows.push_back(r);
                        }
                    }
                    replace_selection(std::move(rows));
                }
                return;
            }
            [[fallthrough]];
        case SelectionMode::Extended:
            if (selection_mode_ == SelectionMode::Extended && shift)
            {
                select_range(positions_[anchor_], position, !ctrl);
                return;
            }
            if (selection_mode_ == SelectionMode::Multiple || ctrl)
            {
                std::vector<std::size_t> rows = selection_;
                if (is_row_selected(row))
                {
                    std::erase(rows, row);
                }
                else
                {
                    rows.push_back(row);
                }
                replace_selection(std::move(rows));
            }
            else
            {
                replace_selection({row});
            }
            anchor_ = row;
            return;
    }
}

void TableViewControl::cursor_moved(std::size_t position, bool shift, bool ctrl)
{
    const std::size_t row = order_[position];
    cursor_               = row;
    mark_dirty(DirtyFlags::Visual);
    if (anchor_ != detail::NoIndex && row_position(anchor_) == detail::NoIndex)
    {
        anchor_ = detail::NoIndex; // hidden in a collapsed group
    }
    switch (selection_mode_)
    {
        case SelectionMode::None:
            break;
        case SelectionMode::Single:
            replace_selection({row});
            anchor_ = row;
            break;
        case SelectionMode::Multiple:
            if (shift && anchor_ != detail::NoIndex)
            {
                select_range(positions_[anchor_], position, false);
            }
            else if (!shift)
            {
                anchor_ = row;
            }
            break;
        case SelectionMode::Extended:
            if (shift)
            {
                if (anchor_ == detail::NoIndex)
                {
                    anchor_ = row;
                }
                select_range(positions_[anchor_], position, !ctrl);
            }
            else if (!ctrl)
            {
                replace_selection({row});
                anchor_ = row;
            }
            break;
    }
    scroll_into_view(position);
}

// A group header takes the cursor but never the selection.
void TableViewControl::move_cursor(std::size_t position, bool shift, bool ctrl)
{
    if (is_group_header(position))
    {
        cursor_header_ = position_group(position);
        mark_dirty(DirtyFlags::Visual);
        scroll_into_view(position);
        return;
    }
    cursor_header_ = detail::NoIndex;
    cursor_moved(position, shift, ctrl);
}

std::size_t TableViewControl::cursor_position() const
{
    if (cursor_header_ != detail::NoIndex)
    {
        return cursor_header_ < groups_.size() ? groups_[cursor_header_].header : detail::NoIndex;
    }
    const std::size_t keyed = selection_mode_ == SelectionMode::Single && first_selected_row() != detail::NoIndex
                                  ? first_selected_row()
                                  : cursor_;
    return row_position(keyed);
}

void TableViewControl::scroll_into_view(std::size_t position)
{
    if (position == detail::NoIndex || row_height_ <= 0.0f)
    {
        return;
    }
    const float item_top        = list_->item_top(position);
    const float item_bottom     = item_top + list_->item_extent(position);
    const float viewport_height = list_->bounds().height;

    float offset = list_->scroll_offset();
    if (item_top < offset)
    {
        offset = item_top;
    }
    else if (item_bottom > offset + viewport_height)
    {
        offset = item_bottom - viewport_height;
    }
    if (offset != list_->scroll_offset())
    {
        list_->set_scroll_offset(offset);
        after_vertical_scroll();
    }
}

// Auto columns size to the cells in view, so rows scrolled in may widen them; template cells that
// left the view go.
void TableViewControl::after_vertical_scroll()
{
    update_sticky_header();
    prune_cells();
    if (std::ranges::any_of(columns_, [](const TableViewColumn& c) { return c.width.unit == GridUnit::Auto; }))
    {
        mark_dirty(DirtyFlags::Layout);
    }
}

// ---- appearance ----

bool TableViewControl::wants_horizontal_lines() const
{
    return grid_lines_ == TableViewGridLinesVisibility::All || grid_lines_ == TableViewGridLinesVisibility::Horizontal;
}

bool TableViewControl::wants_vertical_lines() const
{
    return grid_lines_ == TableViewGridLinesVisibility::All || grid_lines_ == TableViewGridLinesVisibility::Vertical;
}

void TableViewControl::set_grid_lines_visibility(TableViewGridLinesVisibility lines)
{
    grid_lines_ = lines;
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::set_alternating_rows(bool on)
{
    alternating_rows_ = on;
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::set_alternating_row_background(Paint paint)
{
    alternating_background_ = std::move(paint);
    mark_dirty(DirtyFlags::Visual);
}

// ---- layout ----

Size TableViewControl::measure_override(Size outer, const MeasureContext& context) const
{
    // The outer border takes its thickness off every side.
    const Size available{std::max(0.0f, outer.width - 2.0f * BorderThickness),
                         std::max(0.0f, outer.height - 2.0f * BorderThickness)};
    measurer_ = &context.measurer;
    theme_    = &context.theme;

    // Content widths for Auto and star columns: the header and the cells of the rows in view,
    // template cells at their own desired width.
    const Thickness hpad         = header_padding(context.theme);
    const Thickness cpad         = cell_padding(context.theme);
    const TextStyle header_style = header_text_style(context.theme);
    const TextStyle cell_style   = cell_text_style(context.theme);
    const float     body_height  = std::max(0.0f, available.height - header_height_);
    std::size_t     first        = 0;
    std::size_t     last         = order_.size();
    if (std::isfinite(body_height) && row_height_ > 0.0f)
    {
        first = std::min(order_.size(), list_->index_at(list_->scroll_offset()));
        last  = std::min(order_.size(), list_->index_at(list_->scroll_offset() + body_height) + 1);
    }
    natural_widths_.assign(columns_.size(), 0.0f);
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        if (columns_[c].width.unit == GridUnit::Dip)
        {
            continue;
        }
        float width = context.measurer.measure(columns_[c].header, header_style).width + hpad.horizontal() +
                      (c == sort_column_ ? SortIndicatorSize : 0.0f);
        for (std::size_t p = first; p < last; ++p)
        {
            const std::size_t row = order_[p];
            if (row == detail::NoIndex)
            {
                continue;
            }
            if (const RealizedRow* cells = realize_cells(row);
                cells != nullptr && c < cells->cells.size() && cells->cells[c])
            {
                width = std::max(width, cells->cells[c]->measure({Unbounded, row_height_}, context).width);
                continue;
            }
            const auto& text = rows_[row];
            if (c < text.size())
            {
                width = std::max(width, context.measurer.measure(text[c], cell_style).width + cpad.horizontal());
            }
        }
        natural_widths_[c] = std::ceil(width);
    }

    resolve_columns(Unbounded);
    const float total_column_width = content_width();

    // An era's horizontal bar takes a gutter below the rows while the columns overflow what the
    // vertical one leaves them, and the rows are measured again above it.
    hbar_.apply_theme(context.theme);
    horizontal_gutter_       = 0.0f;
    const float width        = std::isfinite(available.width) ? available.width : total_column_width;
    Size        body_desired = list_->measure({width, body_height}, context);
    if (const float gutter = hbar_.gutter();
        gutter > 0.0f && std::isfinite(available.width) && total_column_width > available.width - list_->right_gutter())
    {
        horizontal_gutter_ = gutter;
        body_desired       = list_->measure({width, std::max(0.0f, body_height - gutter)}, context);
        body_desired.height += gutter;
    }
    const Size body_available{width, body_height};
    if (empty_content_ && rows_.empty())
    {
        const Size empty    = empty_content_->measure(body_available, context);
        empty_desired_      = empty;
        body_desired.width  = std::max(body_desired.width, empty.width);
        body_desired.height = std::max(body_desired.height, empty.height);
    }

    Size desired{std::max(total_column_width, body_desired.width), header_height_ + body_desired.height};
    desired.width  = std::min(desired.width, available.width) + 2.0f * BorderThickness;
    desired.height = std::min(desired.height, available.height) + 2.0f * BorderThickness;
    return desired;
}

void TableViewControl::arrange_override(const Rect& outer, const ArrangeContext&)
{
    const Rect final_rect = outer.inset(Thickness{BorderThickness});
    retired_.clear(); // the layout pass Window refreshes its tab ring and popups after
    const float columns_room = std::max(0.0f, final_rect.width - list_->right_gutter());
    resolve_columns(columns_room);
    horizontal_offset_ = std::clamp(horizontal_offset_, 0.0f, std::max(0.0f, content_width() - columns_room));

    // The captions, trimmed to the widths just resolved (less the chevron's box on the sorted one).
    header_text_.clear();
    header_text_.reserve(columns_.size());
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        if (measurer_ == nullptr || theme_ == nullptr)
        {
            header_text_.push_back(columns_[c].header);
            continue;
        }
        const float indicator = c == sort_column_ ? SortIndicatorSize : 0.0f;
        const float room      = std::max(0.0f, actual_widths_[c] - header_padding(*theme_).horizontal() - indicator);
        header_text_.push_back(trim_to_width(columns_[c].header, header_text_style(*theme_), room, *measurer_));
    }

    const Rect body{final_rect.left(),
                    final_rect.top() + header_height_,
                    final_rect.width,
                    std::max(0.0f, final_rect.height - header_height_)};
    list_->arrange(Rect{body.left(), body.top(), body.width, std::max(0.0f, body.height - horizontal_gutter_)});
    update_sticky_header();
    prune_cells();
    if (header_automation_)
    {
        header_automation_->sync();
    }

    if (empty_content_ && rows_.empty())
    {
        // The empty content is centered in the body.
        const Size desired = empty_desired_;
        const Size size{std::min(desired.width, body.width), std::min(desired.height, body.height)};
        empty_content_->arrange(Rect{body.left() + (body.width - size.width) * 0.5f,
                                     body.top() + (body.height - size.height) * 0.5f,
                                     size.width,
                                     size.height});
    }
}

// ---- render ----

void TableViewControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const Rect   header_rect{inner_rect().left(), inner_rect().top(), inner_rect().width, header_height_};
    // The tabular header colors only with the subtle-selection package (Theme::fluent()): `palette` is
    // not populated for the era themes, so their header keeps the menu surface.
    const bool  subtle      = detail::controls(theme).list_item_subtle_selection;
    const Color header_bg   = subtle ? theme.palette.tabular_header_background : theme.surfaces.menu;
    const Color header_text = !is_enabled() ? theme.colors().text_disabled
                              : subtle      ? theme.palette.text_fill_secondary
                                            : theme.surfaces.menu_text;
    // The sort chevron is in the primary text color.
    const Color chevron = !is_enabled() ? theme.colors().text_disabled
                          : subtle      ? theme.palette.text_fill_primary
                                        : theme.surfaces.menu_text;
    // The strip follows the border's rounded top corners: a rounded band clipped to its top half,
    // then the rest square, so a translucent fill never covers a pixel twice.
    const float inner_radius = std::min(std::max(0.0f, theme.corner_radius - BorderThickness), header_rect.height * 0.5f);
    if (inner_radius > 0.0f)
    {
        renderer.push_clip(Rect{header_rect.left(), header_rect.top(), header_rect.width, inner_radius});
        renderer.fill_rounded_rect(Rect{header_rect.left(), header_rect.top(), header_rect.width, 2.0f * inner_radius},
                                   inner_radius,
                                   header_bg);
        renderer.pop_clip();
    }
    renderer.fill_rounded_rect(Rect{header_rect.left(),
                                    header_rect.top() + inner_radius,
                                    header_rect.width,
                                    header_rect.height - inner_radius},
                               0.0f,
                               header_bg);

    const Thickness padding     = header_padding(theme);
    const TextStyle style       = header_text_style(theme);
    const bool      vertical    = wants_vertical_lines();
    const Color     line_color  = grid_line_color(theme);
    const auto      draw_header = [&](std::size_t c)
    {
        if (c >= actual_widths_.size())
        {
            return;
        }
        const float left      = inner_rect().left() + column_left(c);
        const float width     = actual_widths_[c];
        const bool  sorted    = c == sort_column_ && sort_direction_ != TableViewSortDirection::None;
        const float indicator = sorted ? SortIndicatorSize : 0.0f;
        const Rect  text_rect{left + padding.left,
                              header_rect.top(),
                              std::max(0.0f, width - padding.horizontal() - indicator),
                              header_rect.height};
        // The trimmed caption from arrange(); the raw one before the first arrange.
        renderer.draw_text(c < header_text_.size() ? header_text_[c] : columns_[c].header, text_rect, header_text, style);
        if (sorted)
        {
            // The chevron sits on the header's trailing edge, in its own box.
            const Rect box{left + width - SortIndicatorSize,
                           header_rect.top() + (header_rect.height - SortIndicatorSize) * 0.5f,
                           SortIndicatorSize,
                           SortIndicatorSize};
            draw_icon(renderer,
                      sort_direction_ == TableViewSortDirection::Ascending ? SortAscendingGlyph : SortDescendingGlyph,
                      box,
                      chevron,
                      SortIndicatorGlyphSize,
                      theme.icon_font_family);
        }
        if (vertical)
        {
            renderer.stroke_line(Point{left + width, header_rect.top()},
                                 Point{left + width, header_rect.bottom()},
                                 line_color,
                                 1.0f);
        }
        if (c == resizing_)
        {
            // The accent line on the divider being dragged.
            renderer.stroke_line(Point{left + width, header_rect.top()},
                                 Point{left + width, header_rect.bottom()},
                                 subtle ? theme.colors().accent : theme.colors().selection,
                                 2.0f);
        }
    };

    for_each_column_in_paint_order(renderer, header_rect.top(), header_rect.height, draw_header);
    renderer.stroke_line(Point{inner_rect().left(), header_rect.bottom()},
                         Point{inner_rect().right(), header_rect.bottom()},
                         subtle ? theme.palette.tabular_grid_line : theme.surfaces.separator_dark,
                         1.0f);

    list_->render(renderer, context);
    if (sticky_row_ && sticky_rect_.height > 0.0f)
    {
        renderer.push_clip(list_->viewport_rect());
        sticky_row_->render(renderer, context);
        renderer.pop_clip();
    }
    if (empty_content_ && rows_.empty())
    {
        empty_content_->render(renderer, context);
    }
    if (hbar_shown())
    {
        hbar_.render(renderer, theme, hbar_viewport(), content_width(), horizontal_offset_);
    }
    if (horizontal_gutter_ > 0.0f && list_->right_gutter() > 0.0f)
    {
        // The cell where the two era bars meet.
        const Rect body = body_rect();
        renderer.fill_rounded_rect(Rect{body.right() - list_->right_gutter(),
                                        body.bottom() - horizontal_gutter_,
                                        list_->right_gutter(),
                                        horizontal_gutter_},
                                   0.0f,
                                   detail::controls(theme).gutter_scroll_bar->corner);
    }

    // The grid line color round the whole table, inside the bounds.
    stroke_rounded_rect_inside(renderer, bounds(), theme.corner_radius, grid_line_color(theme), BorderThickness);
}

TableViewControl::RingPlace TableViewControl::ring_place() const
{
    if (editor_ != nullptr)
    {
        return {}; // an open editor shows focus itself
    }
    const Rect body     = list_->viewport_rect();
    const auto row_rect = [&](std::size_t position)
    {
        const float top = body.top() + list_->item_top(position) - list_->scroll_offset();
        return Rect{body.left(), top, body.width, list_->item_extent(position)};
    };
    // A ring on a part of the table (a row, a cell, a header cell) goes inside it, cut to the
    // body or the header strip: outside, the next row would cover it.
    const auto inside = [&](const Rect& rect, const Rect& view, bool rounded) -> std::optional<RingPlace>
    {
        const Rect visible = intersection(rect, view);
        return visible.empty() ? std::nullopt : std::optional<RingPlace>{RingPlace{visible, true, rounded}};
    };
    // A column header with the keyboard on it.
    if (header_cursor_ < actual_widths_.size())
    {
        const Rect strip{inner_rect().left(), inner_rect().top(), inner_rect().width, header_height_};
        return inside(Rect{strip.left() + column_left(header_cursor_), strip.top(), actual_widths_[header_cursor_], header_height_},
                      strip,
                      false)
            .value_or(RingPlace{});
    }
    // A group header with the cursor is ringed whatever the mode.
    if (cursor_header_ != detail::NoIndex)
    {
        if (const auto place = inside(row_rect(cursor_position()), body, true))
        {
            return *place;
        }
    }
    // While editing is possible the current cell is what the keys act on, so it takes the ring.
    if (cursor_header_ == detail::NoIndex && !read_only_ && current_column_ < actual_widths_.size())
    {
        if (const std::size_t position = cursor_position(); position != detail::NoIndex)
        {
            const Rect row = row_rect(position);
            if (const auto place = inside(Rect{inner_rect().left() + column_left(current_column_),
                                               row.top(),
                                               actual_widths_[current_column_],
                                               row.height},
                                          body,
                                          false))
            {
                return *place;
            }
        }
    }
    // Single selection is the cursor, so the ring goes round the grid; the other modes ring the
    // row the arrow keys are on.
    if (cursor_header_ == detail::NoIndex && selection_mode_ != SelectionMode::Single && cursor_ != detail::NoIndex &&
        row_position(cursor_) != detail::NoIndex)
    {
        if (const auto place = inside(row_rect(row_position(cursor_)), body, true))
        {
            return *place;
        }
    }
    return RingPlace{bounds(), false, false};
}

Rect TableViewControl::focus_ring_rect(const Theme& /*theme*/) const
{
    return ring_place().rect;
}

float TableViewControl::focus_ring_corner_radius(const Theme& theme) const
{
    return ring_place().rounded ? theme.corner_radius : 0.0f;
}

bool TableViewControl::focus_ring_inside() const
{
    return ring_place().inside;
}

// ---- input ----

bool TableViewControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    // The horizontal scroll bar gets first refusal, as the vertical one does inside the list.
    if (e.action != PointerAction::Wheel && hbar_shown())
    {
        float offset = horizontal_offset_;
        if (hbar_.handle_pointer(e, hbar_viewport(), content_width(), offset))
        {
            set_horizontal_offset(offset);
            return true;
        }
    }

    // A divider drag in progress owns the pointer (it is captured from the press).
    if (resizing_ != detail::NoIndex)
    {
        if (e.action == PointerAction::Move)
        {
            const TableViewColumn& column = columns_[resizing_];
            const float            width  = clamp_width(resize_start_w_ + (e.position.x - resize_start_x_), column);
            if (column.width.unit != GridUnit::Dip || column.width.value != width)
            {
                set_column_width(resizing_, width);
                if (on_column_resized_)
                {
                    on_column_resized_(TableViewColumnResizedArgs{.column = resizing_, .width = width});
                }
            }
        }
        else if (e.action == PointerAction::Up && e.button == PointerButton::Left)
        {
            resizing_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
        }
        return false;
    }

    // A press anywhere else in the table closes an open edit first, as focus moving off the editor
    // does; a rejected value keeps the editor, and the press goes no further.
    const bool opened_now = e.action == PointerAction::Down && std::exchange(opened_by_press_, false);
    if (editor_ != nullptr && !opened_now && e.action == PointerAction::Down && e.button == PointerButton::Left &&
        bounds().contains(e.position) && !editor_->bounds().contains(e.position) && !commit_edit())
    {
        return false;
    }

    const bool in_header = header_at(e.position) != detail::NoIndex;
    if (e.action == PointerAction::Down && header_cursor_ != detail::NoIndex)
    {
        header_cursor_ = detail::NoIndex; // the pointer takes over from the keyboard
        mark_dirty(DirtyFlags::Visual);
    }
    if (e.action == PointerAction::Down && e.button == PointerButton::Left && in_header)
    {
        if (const std::size_t divider = divider_at(e.position); divider != detail::NoIndex)
        {
            resizing_       = divider;
            resize_start_x_ = e.position.x;
            resize_start_w_ = actual_widths_[divider];
            lock_star_columns_before(divider);
            mark_dirty(DirtyFlags::Visual);
            return false;
        }
        const std::size_t header = header_at(e.position);
        if (can_user_sort_columns_ && columns_[header].can_sort)
        {
            pressed_header_ = header;
        }
        return false;
    }
    if (e.action == PointerAction::Up && e.button == PointerButton::Left && pressed_header_ != detail::NoIndex)
    {
        const std::size_t pressed = pressed_header_;
        pressed_header_           = detail::NoIndex;
        if (header_at(e.position) == pressed)
        {
            toggle_sort_direction(pressed);
        }
        return false;
    }

    update_sticky_header();
    prune_cells();
    return false;
}

void TableViewControl::handled_pointer_override(const PointerEvent& e)
{
    (void)pointer_override(e);
}

void TableViewControl::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    // The pinned group header takes a press on it; the rows under it do not.
    if (sticky_row_ && sticky_rect_.height > 0.0f)
    {
        out.push_back(sticky_row_.get());
    }
    if (empty_content_ && rows_.empty())
    {
        out.push_back(empty_content_.get());
    }
    for (auto it = realized_headers_.rbegin(); it != realized_headers_.rend(); ++it)
    {
        out.push_back(it->second.get());
    }
    if (item_menu_ && detail::TreeAccess::popup_request(*item_menu_) != nullptr)
    {
        out.push_back(item_menu_.get());
    }
    // The rows list their own cells and the editor.
    out.push_back(list_.get());
}

bool TableViewControl::covers_children_at(Point p) const
{
    // The horizontal scroll bar sits over the rows.
    return hbar_shown() && hbar_.scrollable(hbar_viewport(), content_width()) &&
           hbar_.band_bounds(hbar_viewport()).contains(p);
}

bool TableViewControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    // Shift turns the wheel sideways only while there is something to scroll sideways.
    if (e.horizontal_wheel || (e.modifiers.shift && horizontal_extent() > 0.0f))
    {
        // Sideways: the columns, when they overflow; otherwise left to whatever scrolls around.
        if (!bounds().contains(e.position) || horizontal_extent() <= 0.0f)
        {
            return false;
        }
        const float before = horizontal_offset_;
        set_horizontal_offset(horizontal_offset_ + e.wheel_delta);
        return horizontal_offset_ != before;
    }
    return false; // the rows scrolled already when they could
}

bool TableViewControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    // An open editor has the keys: Enter and Escape close it, the rest are its own (navigation
    // would scroll the edited row away under it).
    if (editor_ != nullptr)
    {
        if (!is_enabled() || !is_focused())
        {
            return handled;
        }
        if (e.action == KeyAction::Down && e.key == Key::Enter)
        {
            (void)commit_edit(); // a rejected value keeps the editor, and the key
            return handled;
        }
        if (e.action == KeyAction::Down && e.key == Key::Escape)
        {
            // An Escape that closed the editor's own popup was the editor's.
            if (!std::exchange(escape_closes_popup_, false))
            {
                (void)cancel_edit();
            }
            return handled;
        }
        if (edit_target_ != nullptr)
        {
            edit_target_->handle_key(e);
        }
        return handled;
    }

    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }
    if (header_cursor_ != detail::NoIndex)
    {
        handle_header_key(e);
        return handled;
    }
    if (order_.empty())
    {
        if (e.key == Key::ArrowUp && !columns_.empty())
        {
            set_header_cursor(current_column_ != detail::NoIndex ? current_column_ : 0);
        }
        return handled;
    }

    const std::size_t count   = order_.size();
    const std::size_t current = cursor_position();
    const auto        page    = static_cast<std::size_t>(
        std::max(1.0f, row_height_ > 0.0f ? std::floor(list_->bounds().height / row_height_) : 1.0f));

    std::optional<std::size_t> target;
    switch (e.key)
    {
        case Key::ArrowUp:
            if (current == 0 && !columns_.empty() && !e.modifiers.shift && !e.modifiers.ctrl)
            {
                set_header_cursor(current_column_ != detail::NoIndex ? current_column_ : 0);
                return handled;
            }
            target = current == detail::NoIndex ? 0 : current - 1;
            break;
        case Key::ArrowDown:
            target = current == detail::NoIndex ? 0 : std::min(current + 1, count - 1);
            break;
        case Key::Home:
            target = 0;
            break;
        case Key::End:
            target = count - 1;
            break;
        case Key::PageUp:
            target = current == detail::NoIndex ? 0 : (current > page ? current - page : 0);
            break;
        case Key::PageDown:
            target = current == detail::NoIndex ? std::min(page - 1, count - 1) : std::min(current + page, count - 1);
            break;
        case Key::F2:
        {
            // The keyboard's row and the current column; the first editable column before any.
            const std::size_t row = current != detail::NoIndex ? row_at(current) : row_at(first_row_position());
            if (row != detail::NoIndex && !read_only_)
            {
                (void)begin_edit(row, current_column_ != detail::NoIndex ? current_column_ : first_editable_column());
            }
            return handled;
        }
        case Key::Enter:
            if (cursor_header_ != detail::NoIndex)
            {
                toggle_group(cursor_header_);
            }
            return handled;
        case Key::Space:
        {
            if (cursor_header_ != detail::NoIndex)
            {
                toggle_group(cursor_header_);
                return handled;
            }
            const std::size_t position = current == detail::NoIndex ? first_row_position() : current;
            if (position == detail::NoIndex || order_[position] == detail::NoIndex)
            {
                return handled;
            }
            const std::size_t row = order_[position];
            cursor_               = row;
            if (selection_mode_ == SelectionMode::Multiple ||
                (selection_mode_ == SelectionMode::Extended && e.modifiers.ctrl))
            {
                std::vector<std::size_t> rows = selection_;
                if (is_row_selected(row))
                {
                    std::erase(rows, row);
                }
                else
                {
                    rows.push_back(row);
                }
                replace_selection(std::move(rows));
            }
            else
            {
                replace_selection({row});
            }
            anchor_ = row;
            mark_dirty(DirtyFlags::Visual);
            return handled;
        }
        case Key::ArrowLeft:
        case Key::ArrowRight:
        {
            const bool right = e.key == Key::ArrowRight;
            if (cursor_header_ != detail::NoIndex)
            {
                // As in a TreeView: Right expands, Left collapses.
                set_group_expanded(cursor_header_, right);
            }
            else if (!read_only_ && !columns_.empty())
            {
                const std::size_t column = current_column_ == detail::NoIndex ? 0
                                           : right                ? std::min(current_column_ + 1, columns_.size() - 1)
                                           : current_column_ == 0 ? 0
                                                                  : current_column_ - 1;
                set_current_column(column);
            }
            else
            {
                set_horizontal_offset(horizontal_offset_ + (right ? HorizontalKeyStep : -HorizontalKeyStep));
            }
            return handled;
        }
        default:
            return handled;
    }
    move_cursor(*target, e.modifiers.shift, e.modifiers.ctrl);
    return handled;
}

void TableViewControl::handle_header_key(const KeyEvent& e)
{
    const std::size_t column = std::min(header_cursor_, columns_.size() - 1);
    switch (e.key)
    {
        case Key::ArrowLeft:
        case Key::ArrowRight:
        {
            const bool right = e.key == Key::ArrowRight;
            if (e.modifiers.ctrl)
            {
                resize_column_by(column, right ? HeaderKeyResizeStep : -HeaderKeyResizeStep);
            }
            else
            {
                set_header_cursor(right ? std::min(column + 1, columns_.size() - 1) : (column == 0 ? 0 : column - 1));
            }
            return;
        }
        case Key::Home:
            set_header_cursor(0);
            return;
        case Key::End:
            set_header_cursor(columns_.size() - 1);
            return;
        case Key::Enter:
        case Key::Space:
            if (can_user_sort_columns_ && columns_[column].can_sort)
            {
                (void)toggle_sort_direction(column);
            }
            return;
        case Key::ArrowDown:
            header_cursor_  = detail::NoIndex;
            current_column_ = column;
            mark_dirty(DirtyFlags::Visual);
            if (!order_.empty())
            {
                move_cursor(0, false, false);
            }
            return;
        default:
            return;
    }
}

void TableViewControl::set_header_cursor(std::size_t column)
{
    if (columns_.empty())
    {
        return;
    }
    header_cursor_ = std::min(column, columns_.size() - 1);
    scroll_column_into_view(header_cursor_);
    mark_dirty(DirtyFlags::Visual);
}

void TableViewControl::lock_star_columns_before(std::size_t column)
{
    for (std::size_t c = 0; c < column && c < columns_.size() && c < actual_widths_.size(); ++c)
    {
        if (columns_[c].width.unit == GridUnit::Fraction)
        {
            columns_[c].width = actual_widths_[c];
        }
    }
}

void TableViewControl::resize_column_by(std::size_t column, float delta)
{
    if (!can_user_resize_columns_ || column >= columns_.size() || column >= actual_widths_.size() ||
        !columns_[column].can_resize)
    {
        return;
    }
    const float width = clamp_width(actual_widths_[column] + delta, columns_[column]);
    if (width == actual_widths_[column])
    {
        return;
    }
    lock_star_columns_before(column);
    set_column_width(column, width);
    scroll_column_into_view(column);
    if (on_column_resized_)
    {
        on_column_resized_(TableViewColumnResizedArgs{.column = column, .width = width});
    }
}

bool TableViewControl::text_override(const TextInputEvent& text_event)
{
    const std::string_view text = text_event.text;
    if (editor_ != nullptr && edit_target_ != nullptr && is_enabled() && is_focused())
    {
        edit_target_->handle_text(text);
    }
    return true;
}

bool TableViewControl::takes_shortcut(const KeyEvent& e) const
{
    // An open editor keeps the text-editing keys an app-wide shortcut would otherwise take.
    if (editor_ == nullptr || edit_target_ == nullptr)
    {
        return false;
    }
    switch (e.key)
    {
        case Key::Delete:
        case Key::Backspace:
        case Key::Home:
        case Key::End:
            return true;
        case Key::A:
        case Key::C:
        case Key::X:
        case Key::V:
        case Key::Z:
        case Key::Y:
            return e.modifiers.ctrl && !e.modifiers.shift;
        default:
            return false;
    }
}

bool TableViewControl::handle_shortcut(const KeyEvent& e)
{
    // Window closes open popups on Escape before the focused element sees the key.
    if (e.action == KeyAction::Down && e.key == Key::Escape)
    {
        std::vector<Element*> owners;
        if (editor_ != nullptr)
        {
            detail::TreeAccess::collect_live(*editor_, owners);
        }
        escape_closes_popup_ = std::ranges::any_of(owners,
                                                   [](const Element* owner)
                                                   { return detail::TreeAccess::popup_request(*owner) != nullptr; });
    }
    return Element::handle_shortcut(e);
}

// ---- focus ----

void TableViewControl::collect_focusables(std::vector<Element*>& out)
{
    if (focusable())
    {
        out.push_back(this);
    }
    if (!is_enabled())
    {
        return; // a disabled table takes no input, its cells' included
    }
    // The template cells in view order, whatever order their rows came into view in.
    std::vector<RealizedRow*> rows;
    rows.reserve(realized_.size());
    for (RealizedRow& entry : realized_)
    {
        rows.push_back(&entry);
    }
    for (const auto& entry : realized_headers_)
    {
        out.push_back(entry.second.get());
    }
    std::ranges::sort(rows, {}, [this](const RealizedRow* entry) { return row_position(entry->row); });
    for (RealizedRow* entry : rows)
    {
        for (const auto& cell : entry->cells)
        {
            if (cell)
            {
                cell->collect_focusables(out);
            }
        }
    }
    if (empty_content_ && rows_.empty())
    {
        empty_content_->collect_focusables(out);
    }
}

// ---- tree ----

bool TableViewControl::raise_context_requested(std::optional<Point> position)
{
    std::size_t place = detail::NoIndex;
    if (position)
    {
        const Rect body = list_->viewport_rect();
        if (row_height_ > 0.0f && header_at(*position) == detail::NoIndex && body.contains(*position))
        {
            place = list_->index_at(position->y - body.top() + list_->scroll_offset());
        }
    }
    else
    {
        place = cursor_position();
    }
    const std::size_t row = row_at(place);
    if (item_context_menu_ && row != detail::NoIndex && row < rows_.size())
    {
        std::vector<MenuItem> entries = item_context_menu_(row);
        if (!entries.empty())
        {
            if (selection_mode_ != SelectionMode::None && !is_row_selected(row))
            {
                select_row(row);
            }
            const Rect  body = list_->viewport_rect();
            const float top  = body.top() + list_->item_top(place) - list_->scroll_offset();
            detail::show_context_menu(item_menu_,
                                      *this,
                                      std::move(entries),
                                      position.value_or(Point{body.left() + body.width * 0.5f, top + row_height_ * 0.5f}));
            return true;
        }
    }
    return Element::raise_context_requested(position);
}

void TableViewControl::clear_dirty_override(DirtyFlags flags)
{
    if (item_menu_)
    {
        clear_dirty_of(*item_menu_, flags); // listed as a child only while it shows
    }
}

void TableViewControl::for_each_child(ChildVisitor visit) const
{
    visit(*list_);
    if (item_menu_ && detail::TreeAccess::popup_request(*item_menu_) != nullptr)
    {
        visit(*item_menu_);
    }
    for (const auto& entry : realized_headers_)
    {
        visit(*entry.second);
    }
    if (empty_content_ && rows_.empty())
    {
        visit(*empty_content_);
    }
    for (const RealizedRow& entry : realized_)
    {
        for (const auto& cell : entry.cells)
        {
            if (cell)
            {
                visit(*cell);
            }
        }
    }
    if (editor_)
    {
        visit(*editor_);
    }
}

void TableViewControl::tick_override(Duration delta)
{
    if (hbar_.update(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (const std::optional<Duration> wait = hbar_.fade_wait();
        wait && hbar_shown() && hbar_.scrollable(hbar_viewport(), content_width()))
    {
        request_tick_in(*wait); // a shown bar waiting to fade wakes the window once for it
    }
    // Focus left while an editor was open and has not come back: the edit commits (and stays open
    // when its value is rejected).
    if (std::exchange(focus_commit_pending_, false) && editor_ != nullptr && !is_focused())
    {
        (void)commit_edit();
    }
}

// ---- automation ----

Element* TableViewControl::realized_row_at(std::size_t position) const
{
    for (std::size_t i = 0; i < list_->visible_count(); ++i)
    {
        if (auto* row = dynamic_cast<TableViewRow*>(list_->visible_item(i)); row != nullptr && row->position() == position)
        {
            return row;
        }
    }
    return nullptr;
}

Element* TableViewControl::header_item_automation(std::size_t column) const
{
    if (!header_automation_)
    {
        header_automation_ = std::make_unique<TableViewHeaderAutomation>(
            const_cast<TableViewControl*>(this)); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    }
    header_automation_->sync();
    return header_automation_->item(column);
}

Element* TableViewControl::automation_focus() const
{
    return cursor_ < rows_.size() ? realized_row_at(row_position(cursor_)) : nullptr;
}

void TableViewControl::automation_children(std::vector<Element*>& out) const
{
    if (empty_content_ && rows_.empty())
    {
        out.push_back(empty_content_.get());
    }
    if (!columns_.empty())
    {
        (void)header_item_automation(0);
        out.push_back(header_automation_.get());
    }
    for (std::size_t i = 0; i < list_->visible_count(); ++i)
    {
        out.push_back(list_->visible_item(i));
    }
    // A template cell or an editor kept while its row is out of view (it holds focus) has no cell
    // to sit under, so it is the table's own child.
    for (const RealizedRow& entry : realized_)
    {
        if (realized_row_at(row_position(entry.row)) != nullptr)
        {
            continue;
        }
        for (const auto& cell : entry.cells)
        {
            if (cell)
            {
                out.push_back(cell.get());
            }
        }
    }
    if (editor_ && realized_row_at(row_position(edit_row_)) == nullptr)
    {
        out.push_back(editor_.get());
    }
}

Element* TableViewControl::automation_parent_of(const Element& child) const
{
    const auto cell_of = [this](std::size_t row, std::size_t column) -> Element*
    {
        auto* realized_row = static_cast<TableViewRow*>(realized_row_at(row_position(row)));
        return realized_row != nullptr ? realized_row->cell_automation(column) : nullptr;
    };
    if (&child == editor_.get())
    {
        return cell_of(edit_row_, edit_column_);
    }
    for (const RealizedRow& entry : realized_)
    {
        for (std::size_t c = 0; c < entry.cells.size(); ++c)
        {
            if (entry.cells[c].get() == &child)
            {
                return cell_of(entry.row, c);
            }
        }
    }
    for (const auto& [group, content] : realized_headers_)
    {
        if (content.get() == &child && group < groups_.size())
        {
            return realized_row_at(groups_[group].header);
        }
    }
    return nullptr;
}

Element* TableViewControl::grid_item(std::size_t row, std::size_t column) const
{
    if (column >= columns_.size() || is_group_header(row))
    {
        return nullptr;
    }
    auto* realized_row = static_cast<TableViewRow*>(realized_row_at(row));
    return realized_row != nullptr ? realized_row->cell_automation(column) : nullptr;
}

void TableViewControl::table_column_headers(std::vector<Element*>& out) const
{
    for (std::size_t c = 0; c < columns_.size(); ++c)
    {
        out.push_back(header_item_automation(c));
    }
}

void TableViewControl::selected_items(std::vector<Element*>& out) const
{
    for (std::size_t i = 0; i < list_->visible_count(); ++i)
    {
        Element* row = list_->visible_item(i);
        if (const SelectionItemProvider* item = detail::TreeAccess::automation_providers(*row).selection_item;
            item != nullptr && item->item_selected())
        {
            out.push_back(row);
        }
    }
}

} // namespace ui

// SPDX-License-Identifier: MIT
#include "winui/ui/text/TextBox.hpp"

#include "../../platform/Backend.hpp"
#include "TextControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cassert>
#include <cmath>
#include <cstddef>

namespace ui
{

using detail::FieldMinHeight;
using detail::SquiggleAmplitude;
using detail::SquiggleStrokeWidth;
using detail::SquiggleWavelength;
using detail::text_padding;
using detail::visible_line_end;

TextBoxControl::TextBoxControl()
{
    detail::link_spell_checker(); // the OS checker is linked only into an app with a text field
    // The text field's minimum width. The minimum height is applied in measure_override() so a
    // DensityScope can lower it.
    set_min_width(64.0f);
}

TextBoxControl::TextBoxControl(std::string text) :
    text_(std::move(text)),
    caret_(text_.size()),
    selection_anchor_(text_.size())
{
    detail::link_spell_checker(); // the OS checker is linked only into an app with a text field
    set_min_width(64.0f);
}

namespace
{
// The delete button: a 30 DIP column, the button inset by 0,4,4,4, a 12px Cancel glyph.
constexpr float     DeleteButtonWidth  = 30.0f;
constexpr Thickness DeleteButtonMargin = Edges{.top = 4.0f, .right = 4.0f, .bottom = 4.0f};
constexpr IconGlyph DeleteGlyph{0xE894, "Segoe Fluent Icons", 12.0f};
// The line-number column: the numbers, then this much room before the text.
constexpr float LineNumberGap = 16.0f;
constexpr float LineNumberEnd = 12.0f; // from the numbers' right edge to the text

bool is_line_break(char ch)
{
    return ch == '\n';
}

// The caret stops of `text`: every cluster boundary after 0, up to and including its end.
std::vector<std::size_t> caret_stops(std::string_view text)
{
    std::vector<std::size_t> out;
    const TextAnalyzer&      analyzer = text_analyzer();
    for (std::size_t i = 0; i < text.size();)
    {
        i = analyzer.next_cluster(text, i);
        out.push_back(i);
    }
    return out;
}
} // namespace

class TextBoxControl::SelectionWatch
{
public:
    explicit SelectionWatch(TextBoxControl& box) : box_(box), caret_(box.caret_), anchor_(box.selection_anchor_)
    {
        ++box_.watch_depth_;
    }
    SelectionWatch(const SelectionWatch&)            = delete;
    SelectionWatch& operator=(const SelectionWatch&) = delete;
    ~SelectionWatch()
    {
        if (--box_.watch_depth_ > 0 || (box_.caret_ == caret_ && box_.selection_anchor_ == anchor_))
        {
            return;
        }
        box_.follow_caret_ = true;
        box_.mark_dirty(DirtyFlags::Visual);
        if (box_.on_selection_changed_)
        {
            box_.on_selection_changed_();
        }
    }

private:
    TextBoxControl& box_;
    std::size_t     caret_;
    std::size_t     anchor_;
};

TextStyle TextBoxControl::text_style(const Theme& theme) const
{
    TextStyle s = theme.text_style;
    if (font_family_)
    {
        s.font_family = *font_family_;
    }
    if (font_size_)
    {
        s.font_size = *font_size_;
    }
    if (font_weight_)
    {
        s.weight = *font_weight_;
    }
    if (italic_)
    {
        s.italic = true;
    }
    s.character_spacing = character_spacing_;
    return s;
}

std::string TextBoxControl::display_text() const
{
    return password_char_ ? masked(code_point_count(text_)) : text_;
}

std::string TextBoxControl::masked(std::size_t code_points) const
{
    const std::string one = encode_utf8(password_char_.value_or(U'\x25CF'));
    std::string       out;
    out.reserve(one.size() * code_points);
    for (std::size_t i = 0; i < code_points; ++i)
    {
        out += one;
    }
    return out;
}

void TextBoxControl::set_read_only(bool read_only)
{
    if (read_only_ == read_only)
    {
        return;
    }
    read_only_ = read_only;
    if (read_only_)
    {
        spelling_errors_.clear();
        spell_check_pending_ = false;
        last_autocorrection_.reset();
    }
    else if (spell_checker_ && spelling_on())
    {
        schedule_spell_check(0.0);
    }
    mark_dirty(DirtyFlags::Visual);
}

void TextBoxControl::set_password_char(std::optional<char32_t> mask)
{
    password_char_ = mask;
    if (mask)
    {
        spelling_errors_.clear();
        spell_check_pending_ = false;
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

bool TextBoxControl::delete_button_visible() const
{
    return delete_button_enabled_ && theme_delete_button_ && is_focused() && editable() && !text_.empty() &&
           !multiline() && bounds().width > font_em_ * 5.0f;
}

Rect TextBoxControl::delete_button_rect() const
{
    if (!delete_button_visible())
    {
        return {};
    }
    return Rect{bounds().right() - trailing_space_ - DeleteButtonWidth, bounds().top(), DeleteButtonWidth, bounds().height};
}

std::optional<Rect> TextBoxControl::caret_bounds() const
{
    if (!is_focused() || !editable())
    {
        return std::nullopt;
    }
    const Rect area = text_area();
    if (multiline())
    {
        sync_layout();
        if (layout_.lines().empty())
        {
            return Rect{area.x, area.y, 1.0f, line_height_ > 0.0f ? line_height_ : area.height};
        }
        const Rect caret = layout_.caret_rect(caret_);
        return Rect{area.x + caret.x - scroll_x_, area.y + caret.top() - scroll_y_, 1.0f, caret.height};
    }
    // As render_override() draws it: a line tall, centered in the text area.
    const float line_height = line_height_ > 0.0f ? std::min(line_height_, area.height) : area.height;
    return Rect{area.left() + prefix_width(caret_, cached_style_),
                area.top() + (area.height - line_height) * 0.5f,
                1.0f,
                line_height};
}

Rect TextBoxControl::text_area() const
{
    Rect area = bounds().inset(padding_);
    // A button's column replaces the right padding (the columns after the text).
    const float buttons = trailing_space_ + (delete_button_visible() ? DeleteButtonWidth : 0.0f);
    if (buttons > 0.0f)
    {
        area.width = std::max(0.0f, bounds().right() - buttons - area.x);
    }
    if (bar_gutter_ > 0.0f)
    {
        area.width = std::max(0.0f, area.width - bar_gutter_);
    }
    if (gutter_width_ > 0.0f && multiline())
    {
        area.x += gutter_width_;
        area.width = std::max(0.0f, area.width - gutter_width_);
    }
    return area;
}

Point TextBoxControl::text_origin() const
{
    const Rect area = text_area();
    return Point{area.x - scroll_x_, area.y - scroll_y_};
}

Rect TextBoxControl::hbar_viewport() const
{
    const Rect  area   = text_area();
    const float bottom = bounds().bottom() - (chromeless_ ? 0.0f : 1.0f); // inside the border
    return Rect{area.x, area.y, area.width, std::max(0.0f, bottom - area.y)};
}

float TextBoxControl::max_scroll_x() const
{
    return wrapping_ == TextWrapping::NoWrap ? std::max(0.0f, layout_.size().width + 1.0f - text_area().width) : 0.0f;
}

void TextBoxControl::relayout_if_needed(const TextLayoutOptions& options) const
{
    const bool changed = layout_.set_content({TextLayoutParagraph{{TextLayoutRun{display_text(), cached_style_}}}});
    if (changed || layout_.lines().empty() || options.max_width != layout_options_.max_width ||
        options.wrapping != layout_options_.wrapping || !(options.base_style == layout_options_.base_style))
    {
        layout_options_ = options;
        layout_.layout(options, *measurer_);
    }
}

Rect TextBoxControl::bar_viewport() const
{
    const Rect  area  = text_area();
    const float right = bounds().right() - (chromeless_ ? 0.0f : 1.0f); // inside the border
    return Rect{area.x, area.y, std::max(0.0f, right - area.x), area.height};
}

void TextBoxControl::sync_layout() const
{
    if (!measurer_)
    {
        return;
    }
    const float       width = bounds().width > 0.0f ? text_area().width : layout_options_.max_width;
    TextLayoutOptions options;
    options.max_width  = wrapping_ != TextWrapping::NoWrap ? width : 0.0f;
    options.wrapping   = wrapping_;
    options.base_style = cached_style_;
    relayout_if_needed(options);
    // An era's permanent scroll bar takes a column from the text while the text overflows; the
    // overlay bar draws over it. Narrower text only gets taller, so one more pass settles it.
    if (bar_.gutter() > 0.0f && bounds().width > 0.0f)
    {
        const bool  overflows = layout_.size().height > text_area().height;
        const float gutter    = overflows ? std::max(0.0f, bar_.gutter() + 1.0f - padding_.right) : 0.0f;
        if (gutter != bar_gutter_)
        {
            bar_gutter_ = gutter;
            sync_layout();
        }
    }
}

std::size_t TextBoxControl::hit_at(Point window_point) const
{
    const Rect area = text_area();
    if (multiline())
    {
        sync_layout();
        return layout_.hit_test(Point{window_point.x - area.x + scroll_x_, window_point.y - area.y + scroll_y_});
    }
    return hit_test(window_point.x - area.x);
}

std::string TextBoxControl::admit(std::string_view text) const
{
    const TextAnalyzer& analyzer = text_analyzer();
    std::string         out      = casing_ == CharacterCasing::Upper   ? analyzer.to_upper(text)
                                   : casing_ == CharacterCasing::Lower ? analyzer.to_lower(text)
                                                                       : std::string{text};
    if (max_length_ > 0)
    {
        const auto [start, end] = selection_range();
        const std::size_t kept  = code_point_count(text_) -
                                  code_point_count(std::string_view{text_}.substr(start, end - start));
        const std::size_t limit = max_length_;
        const std::size_t room  = kept < limit ? limit - kept : 0;
        // Whole clusters while they fit: an accent is never cut off its letter.
        std::size_t cut   = 0;
        std::size_t count = 0;
        while (cut < out.size())
        {
            const std::size_t next = analyzer.next_cluster(out, cut);
            count += code_point_count(std::string_view{out}.substr(cut, next - cut));
            if (count > room)
            {
                break;
            }
            cut = next;
        }
        out.resize(cut);
    }
    return out;
}

void TextBoxControl::insert_text(std::string_view text)
{
    const auto [start, end] = selection_range();
    edit(start, end - start, text, EditKind::Typing);
    caret_            = start + text.size();
    selection_anchor_ = caret_;
}

void TextBoxControl::edit(std::size_t pos, std::size_t count, std::string_view text, EditKind kind)
{
    UndoStep step{pos, text_.substr(pos, count), std::string{text}, caret_, selection_anchor_, kind};
    text_.replace(pos, count, text);
    note_edit(pos, count, text.size());
    report_edit(pos, step.removed, step.inserted);
    follow_caret_ = true;
    redo_.clear();

    // A run of typing, Backspace or Delete joins the step before it. Typing starts a new step at
    // a space after a word, so it is undone a word at a time.
    const auto joins = [&](UndoStep& last)
    {
        if (!joinable_ || kind == EditKind::Other || last.kind != kind)
        {
            return false;
        }
        switch (kind)
        {
            case EditKind::Typing:
            {
                const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n'; };
                if (!step.removed.empty() || last.pos + last.inserted.size() != pos || last.inserted.empty() ||
                    (space(step.inserted.front()) && !space(last.inserted.back())))
                {
                    return false;
                }
                last.inserted += step.inserted;
                return true;
            }
            case EditKind::Backspace:
                if (!last.inserted.empty() || !step.inserted.empty() || pos + step.removed.size() != last.pos)
                {
                    return false;
                }
                last.removed = step.removed + last.removed;
                last.pos     = pos;
                return true;
            case EditKind::Delete:
                if (!last.inserted.empty() || !step.inserted.empty() || pos != last.pos)
                {
                    return false;
                }
                last.removed += step.removed;
                return true;
            case EditKind::Other:
                break;
        }
        return false;
    };
    if (undo_.empty() || !joins(undo_.back()))
    {
        undo_.push_back(std::move(step));
        if (undo_.size() > MaxUndoSteps)
        {
            undo_.erase(undo_.begin());
        }
    }
    joinable_ = kind != EditKind::Other;
}

void TextBoxControl::apply_step(const UndoStep& step, bool backwards)
{
    const std::string& from = backwards ? step.inserted : step.removed;
    const std::string& to   = backwards ? step.removed : step.inserted;
    text_.replace(step.pos, from.size(), to);
    note_edit(step.pos, from.size(), to.size());
    report_edit(step.pos, from, to);
    if (backwards)
    {
        caret_            = std::min(step.caret_before, text_.size());
        selection_anchor_ = std::min(step.anchor_before, text_.size());
    }
    else
    {
        caret_            = step.pos + to.size();
        selection_anchor_ = caret_;
    }
    follow_caret_ = true;
    joinable_     = false;
    typing_word_  = false;
    preferred_x_.reset();
}

void TextBoxControl::undo()
{
    if (undo_.empty())
    {
        return;
    }
    SelectionWatch watch{*this};
    UndoStep       step = std::move(undo_.back());
    undo_.pop_back();
    apply_step(step, true);
    redo_.push_back(std::move(step));
    changed();
}

void TextBoxControl::redo()
{
    if (redo_.empty())
    {
        return;
    }
    SelectionWatch watch{*this};
    UndoStep       step = std::move(redo_.back());
    redo_.pop_back();
    apply_step(step, false);
    undo_.push_back(std::move(step));
    changed();
}

void TextBoxControl::clear_undo_history()
{
    undo_.clear();
    redo_.clear();
    joinable_ = false;
}

void TextBoxControl::select(std::size_t anchor, std::size_t caret)
{
    // Back to the start of the character a byte offset falls in.
    const auto boundary = [&](std::size_t at)
    {
        at = std::min(at, text_.size());
        while (at > 0 && at < text_.size() && (static_cast<unsigned char>(text_[at]) & 0xC0) == 0x80)
        {
            --at;
        }
        return at;
    };
    SelectionWatch watch{*this};
    selection_anchor_ = boundary(anchor);
    caret_            = boundary(caret);
    joinable_         = false;
    typing_word_      = false;
    preferred_x_.reset();
    follow_caret_ = true;
    mark_dirty(DirtyFlags::Visual);
}

void TextBoxControl::select_all()
{
    select(0, text_.size());
}

std::string TextBoxControl::selected_text() const
{
    const auto [start, end] = selection_range();
    return text_.substr(start, end - start);
}

void TextBoxControl::replace_selection(std::string_view text)
{
    SelectionWatch watch{*this};
    joinable_               = false;
    const auto [start, end] = selection_range();
    edit(start, end - start, text);
    caret_            = start + text.size();
    selection_anchor_ = caret_;
    typing_word_      = false;
    preferred_x_.reset();
    changed();
}

void TextBoxControl::move_vertically(int lines, bool extend)
{
    sync_layout();
    if (layout_.lines().empty())
    {
        return;
    }
    const Rect        caret = layout_.caret_rect(caret_);
    const float       x     = preferred_x_.value_or(caret.x);
    const std::size_t line  = layout_.line_of(caret_);
    const std::size_t last  = layout_.lines().size() - 1;
    if ((lines < 0 && line == 0) || (lines > 0 && line == last))
    {
        // Past the first or last line: the start or end of the text, as a Win32 edit does.
        caret_ = lines < 0 ? 0 : text_.size();
    }
    else
    {
        // `lines` away, stopping at the first or last line.
        const auto target = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(line) + lines,
                                                       0,
                                                       static_cast<std::ptrdiff_t>(last));
        caret_            = layout_.offset_at_x(static_cast<std::size_t>(target), x);
    }
    preferred_x_ = x;
    if (!extend)
    {
        selection_anchor_ = caret_;
    }
}

bool TextBoxControl::spelling_on() const
{
    return spell_check_enabled_ && !read_only_ && !password_char_;
}

void TextBoxControl::set_text(std::string text)
{
    // A two-way binding writes the typed text back through here; the caret must stay put.
    if (text == text_)
    {
        return;
    }
    const bool  moved = caret_ != text_.size() || selection_anchor_ != caret_ || text.size() != text_.size();
    std::string old   = std::exchange(text_, std::move(text));
    color_spans_.clear(); // they colored the old text
    report_edit(0, old, text_);
    caret_            = text_.size();
    selection_anchor_ = caret_;
    spelling_errors_.clear();
    last_autocorrection_.reset();
    typing_word_ = false;
    clear_undo_history();
    if (moved && on_selection_changed_)
    {
        on_selection_changed_();
    }
    if (spell_checker_ && spelling_on())
    {
        schedule_spell_check(0.0);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

std::pair<std::size_t, std::size_t> TextBoxControl::selection_range() const
{
    return {std::min(caret_, selection_anchor_), std::max(caret_, selection_anchor_)};
}

bool TextBoxControl::has_selection() const
{
    const auto [start, end] = selection_range();
    return start != end;
}

void TextBoxControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        enter_down_   = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void TextBoxControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        enter_down_  = false; // losing focus cancels a keyboard press, mirroring Button/CheckBox
        typing_word_ = false; // leaving the field finishes the word being typed
        joinable_    = false; // and typing after coming back is a new undo step
    }
    else
    {
        reset_caret_blink(); // start each focus gain fully visible, not mid-cycle
    }
    // Layout too: the delete button's column comes and goes with focus, and a box sized to its
    // text makes room for it.
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TextBoxControl::reset_caret_blink()
{
    caret_visible_  = true;
    caret_blink_ms_ = 0.0;
}

Size TextBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header      = header_.measure(available.width, context);
    available              = ControlHeader::below(available, header);
    const Size description = description_.measure(available.width, context);
    available              = ControlHeader::below(available, description);

    const Thickness padding = text_padding(context.theme);
    measurer_               = &context.measurer;
    bar_.apply_theme(context.theme);
    hbar_.apply_theme(context.theme);
    cached_style_        = text_style(context.theme);
    left_inset_          = padding.left;
    padding_             = padding;
    font_em_             = cached_style_.font_size;
    theme_delete_button_ = detail::controls(context.theme).text_box_delete_button;

    const Size line = context.measurer.measure("Ag", cached_style_);
    line_height_    = line.height;

    // As wide as the text or placeholder plus padding and button
    // columns, so a box grows as it is typed into and an empty one sits at its minimum width.
    // The line-number column fits the last line's number, two digits at least.
    gutter_width_ = 0.0f;
    if (line_numbers_ && multiline())
    {
        const auto        breaks = std::ranges::count(text_, '\n');
        const std::size_t digits = std::max<std::size_t>(2, std::to_string(breaks + 1).size());
        gutter_width_ = static_cast<float>(digits) * context.measurer.measure("0", cached_style_).width + LineNumberGap;
    }
    const float buttons = trailing_space_ + (delete_button_visible() ? DeleteButtonWidth : 0.0f);
    const float chrome  = padding.horizontal() + buttons + gutter_width_;
    const float hint = placeholder_text_.empty() ? 0.0f : context.measurer.measure(placeholder_text_, cached_style_).width;
    float content = line.height;
    float text_w  = 0.0f;
    if (multiline())
    {
        // Laid out at the width the text will get; the box is as tall as its lines.
        TextLayoutOptions options;
        options.wrapping   = wrapping_;
        options.base_style = cached_style_;
        options.max_width  = wrapping_ != TextWrapping::NoWrap && std::isfinite(available.width)
                                 ? std::max(0.0f, available.width - chrome)
                                 : 0.0f;
        relayout_if_needed(options);
        content = std::max(line.height, layout_.size().height);
        text_w  = layout_.size().width;
    }
    else
    {
        text_w = context.measurer.measure(display_text(), cached_style_).width;
    }
    float width = std::max(text_w, hint) + chrome + 1.0f; // and the caret's pixel after the last character
    width       = std::min(width, available.width);
    Size desired{width, content + padding.vertical()};
    desired.height = std::min(desired.height, available.height);
    // The minimum height covers the header too.
    Size total = ControlHeader::above(desired, header);
    if (theme_min_height_)
    {
        total.height = std::max(total.height, context.theme.sizing.text_control_min_height.value_or(FieldMinHeight));
    }
    return ControlHeader::above(total, description);
}

void TextBoxControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    description_.arrange(*this);
    if (multiline())
    {
        sync_layout();
    }
}

float TextBoxControl::prefix_width(std::size_t count, const TextStyle& style) const
{
    if (!measurer_)
    {
        return 0.0f;
    }
    if (password_char_)
    {
        return measurer_->measure(masked(code_point_count(std::string_view{text_}.substr(0, count))), style).width;
    }
    return measurer_->measure(std::string_view{text_}.substr(0, count), style).width;
}

std::size_t TextBoxControl::hit_test(float local_x) const
{
    if (!measurer_ || text_.empty() || local_x <= 0.0f)
    {
        return 0;
    }

    // Binary search over prefix widths, which never decrease for left-to-right text.
    const std::vector<std::size_t> stops = caret_stops(text_);
    std::size_t                    lo    = 0;
    std::size_t                    hi    = stops.size();
    while (lo < hi)
    {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (prefix_width(stops[mid], cached_style_) <= local_x)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    if (lo == stops.size())
    {
        return text_.size();
    }
    // The cluster ending at stops[lo] is the first to reach past local_x; decide whether the
    // click landed in the first or second half of it.
    const std::size_t begin  = lo == 0 ? 0 : stops[lo - 1];
    const float       before = prefix_width(begin, cached_style_);
    const float       after  = prefix_width(stops[lo], cached_style_);
    return (local_x - before <= after - local_x) ? begin : stops[lo];
}

bool TextBoxControl::is_word_char(char32_t ch)
{
    return text_analyzer().is_word_char(ch);
}

bool TextBoxControl::is_spelling_char(char32_t ch)
{
    return is_word_char(ch) || ch == '\'' || ch == U'\x2019'; // ' and ’
}

std::size_t TextBoxControl::word_stop(std::size_t from, bool forward) const
{
    if (password_char_)
    {
        return forward ? text_.size() : 0; // a password has no words to give away
    }
    return forward ? next_word_start(text_, from) : previous_word_start(text_, from);
}

std::pair<std::size_t, std::size_t> TextBoxControl::word_range(std::size_t index) const
{
    if (text_.empty())
    {
        return {0, 0};
    }
    if (password_char_)
    {
        return {0, text_.size()}; // a password has no words to give away
    }
    return word_run(text_, index, false);
}

void TextBoxControl::erase_selection()
{
    const auto [start, end] = selection_range();
    edit(start, end - start, {});
    caret_            = start;
    selection_anchor_ = start;
}

void TextBoxControl::changed()
{
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_text_changed_)
    {
        on_text_changed_(text_);
    }
}

void TextBoxControl::copy_selection(bool cut)
{
    if (!has_selection() || password_char_ || (cut && !editable()))
    {
        return;
    }
    const auto [start, end] = selection_range();
    clipboard().set_text(text_.substr(start, end - start));
    if (cut)
    {
        erase_selection();
        typing_word_ = false;
        changed();
    }
}

void TextBoxControl::paste()
{
    if (!editable())
    {
        return;
    }
    const auto clip = clipboard().text();
    if (!clip)
    {
        return;
    }
    // Drop anything below 0x20, except a line break into a box that accepts returns (\r\n and \r
    // become \n). Byte by byte is safe: no byte of a multi-byte UTF-8 sequence is below 0x80.
    std::string sanitized;
    sanitized.reserve(clip->size());
    for (std::size_t i = 0; i < clip->size(); ++i)
    {
        const char c = (*clip)[i];
        if (static_cast<unsigned char>(c) >= 0x20)
        {
            sanitized += c;
        }
        else if (accepts_return_ && (c == '\n' || c == '\r'))
        {
            if (c == '\r' && i + 1 < clip->size() && (*clip)[i + 1] == '\n')
            {
                ++i;
            }
            sanitized += '\n';
        }
    }

    sanitized = admit(sanitized);
    if (sanitized.empty() && !has_selection())
    {
        return;
    }
    const auto [start, end] = selection_range();
    edit(start, end - start, sanitized);
    caret_            = start + sanitized.size();
    selection_anchor_ = caret_;
    typing_word_      = false;
    changed();
}

// ---- spell checking -----------------------------------------------------------

void TextBoxControl::set_spell_check(bool enabled)
{
    if (enabled == spell_check_enabled_)
    {
        return;
    }
    spell_check_enabled_ = enabled;
    spelling_errors_.clear();
    spell_check_pending_ = false;
    last_autocorrection_.reset();
    if (enabled && spell_checker_)
    {
        schedule_spell_check(0.0);
    }
    mark_dirty(DirtyFlags::Visual);
}

void TextBoxControl::sync_spell_checker(SpellChecker& checker)
{
    if (spell_checker_ == &checker)
    {
        return;
    }
    spell_checker_ = &checker;
    spelling_errors_.clear();
    if (spelling_on())
    {
        schedule_spell_check(0.0); // the first check runs on the next tick, not inside focus handling
    }
}

void TextBoxControl::schedule_spell_check(double delay_ms)
{
    spell_check_pending_  = true;
    spell_check_delay_ms_ = delay_ms;
}

void TextBoxControl::run_spell_check()
{
    spell_check_pending_ = false;
    spelling_errors_.clear();
    if (spell_checker_)
    {
        spell_generation_ = spell_checker_->generation();
        // highlight_enabled() first: with highlighting off there's nothing to show, so don't
        // spend a check (or, the first time, a COM spin-up) on it.
        if (spelling_on() && !text_.empty() && spell_checker_->highlight_enabled() && spell_checker_->available())
        {
            spelling_errors_ = spell_checker_->check(text_);
        }
    }
    mark_dirty(DirtyFlags::Visual);
}

void TextBoxControl::note_edit(std::size_t pos, std::size_t removed, std::size_t inserted)
{
    const std::size_t removed_end = pos + removed;
    // Touching counts: typing onto the end of a flagged word changes that word.
    std::erase_if(spelling_errors_,
                  [&](const SpellingError& e) { return e.start + e.length >= pos && e.start <= removed_end; });
    for (SpellingError& e : spelling_errors_)
    {
        if (e.start > removed_end)
        {
            e.start = e.start - removed + inserted;
        }
    }
    last_autocorrection_.reset();
    if (spell_checker_ && spelling_on())
    {
        schedule_spell_check(SpellCheckDelayMs);
    }
    // Color spans stay on their text: after the edit they move, around it they stretch, and in
    // the removed text they go.
    if (!color_spans_.empty())
    {
        // Text typed at either edge of a span stays outside it.
        const auto moved = [&](std::size_t at, bool is_end)
        {
            if (is_end ? at <= pos : at < pos)
            {
                return at;
            }
            if (at >= removed_end)
            {
                return at - removed + inserted;
            }
            return is_end ? pos : pos + inserted;
        };
        for (TextColorSpan& span : color_spans_)
        {
            span.begin = moved(span.begin, false);
            span.end   = moved(span.end, true);
        }
        std::erase_if(color_spans_, [](const TextColorSpan& span) { return span.begin >= span.end; });
        mark_dirty(DirtyFlags::Visual);
    }
}

void TextBoxControl::set_color_spans(std::vector<TextColorSpan> spans)
{
    std::ranges::sort(spans, {}, &TextColorSpan::begin);
    std::size_t reached = 0;
    for (TextColorSpan& span : spans)
    {
        span.begin = std::max(span.begin, reached); // an overlap is cut off the later span
        span.end   = std::min(span.end, text_.size());
        reached    = std::max(reached, span.end);
    }
    std::erase_if(spans, [](const TextColorSpan& span) { return span.begin >= span.end; });
    if (spans != color_spans_)
    {
        color_spans_ = std::move(spans);
        mark_dirty(DirtyFlags::Visual);
    }
}

void TextBoxControl::report_edit(std::size_t pos, std::string_view removed, std::string_view inserted)
{
    if (on_edit_)
    {
        on_edit_(TextBoxEditArgs{.position = pos, .removed = removed, .inserted = inserted});
    }
}

void TextBoxControl::commit_word_before(std::size_t end)
{
    if (!spell_checker_ || !spelling_on() || end == 0 || end > text_.size())
    {
        return;
    }
    std::size_t start = end;
    while (start > 0 && is_spelling_char(code_point_before(text_, start)))
    {
        start = previous_code_point(text_, start);
    }
    if (start == end)
    {
        return;
    }
    const bool highlight   = spell_checker_->highlight_enabled();
    const bool autocorrect = spell_checker_->autocorrect_enabled();
    if ((!highlight && !autocorrect) || !spell_checker_->available())
    {
        return;
    }

    const std::string word = text_.substr(start, end - start);
    // A fresh answer for this word supersedes whatever the last full check said about it.
    std::erase_if(spelling_errors_, [&](const SpellingError& e) { return e.start < end && e.start + e.length > start; });

    for (SpellingError error : spell_checker_->check(word))
    {
        error.start += start;
        if (error.action == SpellingAction::Replace && autocorrect && !error.replacement.empty())
        {
            std::string original = text_.substr(error.start, error.length);
            if (std::find(rejected_autocorrections_.begin(), rejected_autocorrections_.end(), original) ==
                rejected_autocorrections_.end())
            {
                edit(error.start, error.length, error.replacement);
                // The caret sits after the boundary character, i.e. after the replaced word.
                caret_               = caret_ - error.length + error.replacement.size();
                selection_anchor_    = caret_;
                last_autocorrection_ = Autocorrection{error.start, std::move(original), error.replacement};
                break; // offsets of any further errors in `word` are stale now
            }
        }
        if (highlight)
        {
            spelling_errors_.push_back(std::move(error));
        }
    }
    std::sort(spelling_errors_.begin(),
              spelling_errors_.end(),
              [](const SpellingError& a, const SpellingError& b) { return a.start < b.start; });
    mark_dirty(DirtyFlags::Visual);
}

void TextBoxControl::revert_autocorrection()
{
    const Autocorrection ac = std::move(*last_autocorrection_);
    last_autocorrection_.reset();
    if (ac.start + ac.replacement.size() > text_.size() ||
        text_.compare(ac.start, ac.replacement.size(), ac.replacement) != 0)
    {
        return;
    }
    edit(ac.start, ac.replacement.size(), ac.original);
    if (caret_ >= ac.start + ac.replacement.size())
    {
        caret_ = caret_ - ac.replacement.size() + ac.original.size();
    }
    else
    {
        caret_ = ac.start + ac.original.size();
    }
    selection_anchor_ = caret_;
    typing_word_      = false;
    rejected_autocorrections_.push_back(ac.original);
    // Re-flag the restored word right away rather than after the debounce: it's rejected
    // now, so this squiggles it instead of correcting it again.
    commit_word_before(ac.start + ac.original.size());
    changed();
}

bool TextBoxControl::spelling_shown(const SpellingError& error) const
{
    return !(typing_word_ && error.start <= caret_ && caret_ <= error.start + error.length);
}

const SpellingError* TextBoxControl::spelling_error_at(std::size_t index) const
{
    for (const SpellingError& error : spelling_errors_)
    {
        if (spelling_shown(error) && error.start <= index && index <= error.start + error.length)
        {
            return &error;
        }
    }
    return nullptr;
}

void TextBoxControl::forget_spelling(const std::string& word)
{
    std::erase_if(spelling_errors_,
                  [&](const SpellingError& e)
                  { return text_.compare(e.start, e.length, word) == 0 && e.length == word.size(); });
    mark_dirty(DirtyFlags::Visual);
}

// ---- input ----------------------------------------------------------------------

bool TextBoxControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    SelectionWatch watch{*this};

    // A multi-line box's scroll bar takes its hover, drag and clicks before the text does.
    if (multiline() && e.action != PointerAction::Wheel && !pointer_down_)
    {
        sync_layout();
        float offset = scroll_y_;
        if (bar_.handle_pointer(e, bar_viewport(), layout_.size().height, offset))
        {
            scroll_y_ = std::clamp(offset, 0.0f, std::max(0.0f, layout_.size().height - text_area().height));
            mark_dirty(DirtyFlags::Visual);
            return true;
        }
        float offset_x = scroll_x_;
        if (wrapping_ == TextWrapping::NoWrap &&
            hbar_.handle_pointer(e, hbar_viewport(), layout_.size().width + 1.0f, offset_x))
        {
            scroll_x_ = std::clamp(offset_x, 0.0f, max_scroll_x());
            mark_dirty(DirtyFlags::Visual);
            return true;
        }
    }

    // The delete button takes its own press and release; a click on it clears the text.
    const bool on_delete = delete_button_visible() && delete_button_rect().contains(e.position);
    if (e.action == PointerAction::Move || e.action == PointerAction::Leave)
    {
        const bool hover = e.action == PointerAction::Move && on_delete;
        if (hover != delete_hovered_)
        {
            delete_hovered_ = hover;
            mark_dirty(DirtyFlags::Visual);
        }
    }
    if (e.action == PointerAction::Down && e.button == PointerButton::Left && on_delete)
    {
        delete_pressed_ = true;
        hovered_        = true;
        mark_dirty(DirtyFlags::Visual);
        return true;
    }
    if (e.action == PointerAction::Up && e.button == PointerButton::Left && delete_pressed_)
    {
        delete_pressed_ = false;
        if (on_delete)
        {
            edit(0, text_.size(), {});
            caret_            = 0;
            selection_anchor_ = 0;
            spelling_errors_.clear();
            changed();
        }
        mark_dirty(DirtyFlags::Visual);
        return true;
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = bounds().contains(e.position);
            if (pointer_down_)
            {
                caret_ = hit_at(e.position);
                preferred_x_.reset();
                mark_dirty(DirtyFlags::Visual);
            }
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && bounds().contains(e.position))
            {
                reset_caret_blink();
                typing_word_ = false;
                joinable_    = false;
                last_autocorrection_.reset();
                preferred_x_.reset();
                pointer_down_         = true;
                hovered_              = true;
                handled               = true;
                const std::size_t hit = hit_at(e.position);
                if (e.click_count >= 3)
                {
                    // The line (paragraph) under the pointer; the whole text on a single line.
                    std::size_t start = std::min(hit, text_.size());
                    std::size_t end   = start;
                    while (start > 0 && !is_line_break(text_[start - 1]))
                    {
                        --start;
                    }
                    while (end < text_.size() && !is_line_break(text_[end]))
                    {
                        ++end;
                    }
                    selection_anchor_ = start;
                    caret_            = end;
                }
                else if (e.click_count == 2)
                {
                    const auto [start, end] = word_range(hit);
                    selection_anchor_       = start;
                    caret_                  = end;
                }
                else if (e.modifiers.shift)
                {
                    caret_ = hit; // extend the existing selection from its current anchor
                }
                else
                {
                    caret_            = hit;
                    selection_anchor_ = hit;
                }
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                handled       = pointer_down_;
                pointer_down_ = false;
                hovered_      = bounds().contains(e.position);
            }
            break;

        case PointerAction::Leave:
            hovered_ = false;
            break;

        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool TextBoxControl::key_override(const KeyEvent& e)
{
    SelectionWatch watch{*this};
    const bool     handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused())
    {
        return handled;
    }

    // A box that takes Tab inserts a tab character; Shift+Tab is taken and does nothing.
    if (e.key == Key::Tab && !e.modifiers.ctrl && takes_tab_key())
    {
        if (e.action == KeyAction::Down && !e.modifiers.shift && !admit("\t").empty())
        {
            reset_caret_blink();
            typing_word_ = false;
            preferred_x_.reset();
            insert_text("\t");
            changed();
        }
        return true;
    }
    // A box that accepts returns takes Enter as a line break, and never submits.
    if (e.key == Key::Enter && accepts_return_)
    {
        if (e.action == KeyAction::Down && editable() && !admit("\n").empty())
        {
            reset_caret_blink();
            typing_word_ = false;
            preferred_x_.reset();
            insert_text("\n");
            changed();
        }
        return handled;
    }
    // Enter mirrors Button's Down/Up key bookkeeping (activate on release, not press).
    if (e.key == Key::Enter)
    {
        if (e.action == KeyAction::Down)
        {
            enter_down_  = true;
            typing_word_ = false; // submitting finishes the word being typed
        }
        else if (enter_down_)
        {
            enter_down_ = false;
            if (on_submit_)
            {
                on_submit_();
            }
        }
        return handled;
    }

    if (e.action != KeyAction::Down)
    {
        return handled;
    }
    reset_caret_blink(); // every key handled below moves the caret, edits, or selects
    // Ctrl+Z may only revert an autocorrection as the very next key. Lone modifiers arrive as
    // Key::Unknown and do not count, since Ctrl is pressed on the way to Ctrl+Z.
    if (e.key != Key::Z && e.key != Key::Unknown)
    {
        last_autocorrection_.reset();
    }

    if (e.key != Key::ArrowUp && e.key != Key::ArrowDown)
    {
        preferred_x_.reset();
    }
    mark_dirty(DirtyFlags::Visual);

    // Moving the caret ends a run of typing: what follows is a new undo step.
    if (e.key == Key::ArrowUp || e.key == Key::ArrowDown || e.key == Key::ArrowLeft || e.key == Key::ArrowRight ||
        e.key == Key::Home || e.key == Key::End || e.key == Key::PageUp || e.key == Key::PageDown ||
        (e.key == Key::A && e.modifiers.ctrl))
    {
        joinable_ = false;
    }

    switch (e.key)
    {
        case Key::ArrowUp:
        case Key::ArrowDown:
            if (multiline())
            {
                typing_word_ = false;
                move_vertically(e.key == Key::ArrowUp ? -1 : 1, e.modifiers.shift);
            }
            break;

        case Key::ArrowLeft:
            typing_word_ = false;
            if (e.modifiers.ctrl)
            {
                caret_ = word_stop(caret_, false);
            }
            else if (!e.modifiers.shift && has_selection())
            {
                caret_ = selection_range().first; // collapse to the near boundary, don't also step
            }
            else if (caret_ > 0)
            {
                caret_ = text_analyzer().previous_cluster(text_, caret_);
            }
            if (!e.modifiers.shift)
            {
                selection_anchor_ = caret_;
            }
            break;

        case Key::ArrowRight:
            typing_word_ = false;
            if (e.modifiers.ctrl)
            {
                caret_ = word_stop(caret_, true);
            }
            else if (!e.modifiers.shift && has_selection())
            {
                caret_ = selection_range().second;
            }
            else if (caret_ < text_.size())
            {
                caret_ = text_analyzer().next_cluster(text_, caret_);
            }
            if (!e.modifiers.shift)
            {
                selection_anchor_ = caret_;
            }
            break;

        case Key::PageUp:
        case Key::PageDown:
            // A view's height of lines, the view moving with the caret so it stays where it was on
            // screen.
            if (multiline())
            {
                typing_word_ = false;
                sync_layout();
                if (layout_.lines().empty())
                {
                    break;
                }
                const float line   = std::max(1.0f, layout_.lines().front().height);
                const int   page   = std::max(1, static_cast<int>(text_area().height / line) - 1);
                const float before = layout_.caret_rect(caret_).top() - scroll_y_;
                move_vertically(e.key == Key::PageUp ? -page : page, e.modifiers.shift);
                scroll_y_ = layout_.caret_rect(caret_).top() - before;
            }
            break;

        case Key::Home:
            typing_word_ = false;
            if (multiline() && !e.modifiers.ctrl)
            {
                sync_layout();
                caret_ = layout_.lines().empty() ? 0 : layout_.lines()[layout_.line_of(caret_)].begin;
            }
            else
            {
                caret_ = 0;
            }
            if (!e.modifiers.shift)
            {
                selection_anchor_ = caret_;
            }
            break;

        case Key::End:
            typing_word_ = false;
            if (multiline() && !e.modifiers.ctrl)
            {
                sync_layout();
                caret_ = layout_.lines().empty() ? text_.size() : visible_line_end(layout_, layout_.line_of(caret_));
            }
            else
            {
                caret_ = text_.size();
            }
            if (!e.modifiers.shift)
            {
                selection_anchor_ = caret_;
            }
            break;

        case Key::Backspace:
            if (!editable())
            {
                break;
            }
            if (has_selection())
            {
                erase_selection();
                changed();
            }
            else if (caret_ > 0)
            {
                // Ctrl+Backspace takes the word before the caret.
                const std::size_t from = e.modifiers.ctrl ? word_stop(caret_, false)
                                                          : text_analyzer().previous_cluster(text_, caret_);
                edit(from, caret_ - from, {}, EditKind::Backspace);
                caret_            = from;
                selection_anchor_ = caret_;
                typing_word_      = true; // still editing this word
                changed();
            }
            break;

        case Key::Delete:
            if (!editable())
            {
                break;
            }
            if (has_selection())
            {
                erase_selection();
                changed();
            }
            else if (caret_ < text_.size())
            {
                // Ctrl+Delete takes the rest of the word and the space after it.
                const std::size_t to = e.modifiers.ctrl ? word_stop(caret_, true)
                                                        : text_analyzer().next_cluster(text_, caret_);
                edit(caret_, to - caret_, {}, EditKind::Delete);
                typing_word_ = true;
                changed();
            }
            break;

        case Key::A:
            if (e.modifiers.ctrl)
            {
                typing_word_      = false;
                selection_anchor_ = 0;
                caret_            = text_.size();
            }
            break;

        case Key::C:
        case Key::X:
            if (e.modifiers.ctrl)
            {
                copy_selection(e.key == Key::X);
            }
            break;

        case Key::V:
            if (e.modifiers.ctrl)
            {
                paste();
            }
            break;

        case Key::Z:
            if (e.modifiers.ctrl && editable())
            {
                if (last_autocorrection_ && !e.modifiers.shift)
                {
                    revert_autocorrection();
                }
                else if (e.modifiers.shift)
                {
                    redo();
                }
                else
                {
                    undo();
                }
            }
            break;

        case Key::Y:
            if (e.modifiers.ctrl && !e.modifiers.shift && editable())
            {
                redo();
            }
            break;

        default:
            break;
    }
    return handled;
}

bool TextBoxControl::takes_shortcut(const KeyEvent& e) const
{
    switch (e.key)
    {
        case Key::Delete:
        case Key::Backspace:
        case Key::Home:
        case Key::End:
        case Key::ArrowLeft:
        case Key::ArrowRight:
            return true;
        case Key::Z:
            return e.modifiers.ctrl; // Ctrl+Shift+Z redoes
        case Key::A:
        case Key::C:
        case Key::X:
        case Key::V:
        case Key::Y:
            return e.modifiers.ctrl && !e.modifiers.shift;
        default:
            return false;
    }
}

bool TextBoxControl::text_override(const TextInputEvent& text_event)
{
    const std::string_view text = text_event.text;
    assert(is_valid_utf8(text));
    // Control characters are handled via handle_key()/WM_KEYDOWN instead; byte by byte is
    // safe, since no byte of a multi-byte sequence is below 0x80.
    std::string typed;
    for (const char c : text)
    {
        if (static_cast<unsigned char>(c) >= 0x20)
        {
            typed += c;
        }
    }
    if (!editable() || !is_focused() || typed.empty())
    {
        return true;
    }
    SelectionWatch watch{*this};
    reset_caret_blink();
    preferred_x_.reset();

    const std::string admitted = admit(typed);
    if (admitted.empty())
    {
        return true; // max_length reached: the keystroke is dropped, as RichEdit drops it
    }
    insert_text(admitted);
    const char32_t last = code_point_before(text_, caret_);
    if (is_spelling_char(last))
    {
        typing_word_ = true;
    }
    else
    {
        // A boundary character finishes the word before it: check (and maybe autocorrect)
        // just that word now, ahead of the debounced full check note_edit() armed.
        typing_word_ = false;
        commit_word_before(previous_code_point(text_, caret_));
    }
    changed();
    return true;
}

bool TextBoxControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    if (!is_enabled())
    {
        return false;
    }
    SelectionWatch watch{*this};
    typing_word_ = false;
    joinable_    = false;
    last_autocorrection_.reset();
    reset_caret_blink();
    // A check scheduled a moment ago (the right-click itself just focused this field) has
    // to land before the menu is built, or a squiggled word would offer no suggestions.
    if (spell_check_pending_)
    {
        run_spell_check();
    }

    const float text_left = text_area().x;
    std::size_t at        = caret_;
    Point       anchor{};
    if (position)
    {
        // Right-click moves the caret, except inside the selection, so Cut/Copy still act
        // on it (Win32 EDIT/RichEdit behave the same way).
        at                      = hit_at(*position);
        const auto [start, end] = selection_range();
        if (!(has_selection() && at >= start && at <= end))
        {
            caret_            = at;
            selection_anchor_ = at;
        }
        anchor = *position;
    }
    else if (multiline())
    {
        sync_layout();
        const Rect caret = layout_.caret_rect(caret_);
        anchor           = Point{text_left + caret.x, text_area().y + caret.bottom() - scroll_y_};
    }
    else
    {
        anchor = Point{text_left + prefix_width(caret_, cached_style_), bounds().bottom()};
    }
    mark_dirty(DirtyFlags::Visual);

    enum class Command
    {
        None,
        Suggestion,
        DeleteWord,
        AddToDictionary,
        Ignore,
        Cut,
        Copy,
        Paste,
        SelectAll,
        Undo,
        Redo,
    };
    std::vector<ContextMenuItem>                 items;
    std::vector<std::pair<Command, std::size_t>> commands; // parallel to items; size_t: suggestion index
    std::vector<std::string>                     suggestions;
    const auto add = [&](std::string text, bool enabled, Command command, std::size_t payload = 0)
    {
        items.push_back(ContextMenuItem{std::move(text), enabled, false});
        commands.emplace_back(command, payload);
    };
    const auto separator = [&]
    {
        items.push_back(ContextMenuItem::separator());
        commands.emplace_back(Command::None, 0);
    };

    // Copied out: applying a command edits text_ and spelling_errors_.
    std::optional<SpellingError> error;
    if (spell_checker_ && spelling_on())
    {
        if (const SpellingError* hit = spelling_error_at(at))
        {
            error = *hit;
        }
    }
    const Strings& t = strings();
    std::string    word;
    if (error)
    {
        word = text_.substr(error->start, error->length);
        if (error->action == SpellingAction::Delete)
        {
            add(t.text_menu.delete_repeated_word, true, Command::DeleteWord);
        }
        else
        {
            if (error->action == SpellingAction::Replace && !error->replacement.empty())
            {
                suggestions.push_back(error->replacement);
            }
            for (std::string& s : spell_checker_->suggest(word))
            {
                if (suggestions.size() >= MaxSpellingSuggestions)
                {
                    break;
                }
                if (std::find(suggestions.begin(), suggestions.end(), s) == suggestions.end())
                {
                    suggestions.push_back(std::move(s));
                }
            }
            for (std::size_t i = 0; i < suggestions.size(); ++i)
            {
                add(suggestions[i], true, Command::Suggestion, i);
            }
            if (suggestions.empty())
            {
                add(t.text_menu.no_spelling_suggestions, false, Command::None);
            }
        }
        separator();
        add(t.text_menu.add_to_dictionary, true, Command::AddToDictionary);
        add(t.text_menu.ignore, true, Command::Ignore);
        separator();
    }
    // A password offers neither Cut nor Copy (PasswordBox's menu is Paste and Select All), a
    // read-only box nothing that edits.
    const bool can_copy = has_selection() && !password_char_;
    if (!password_char_)
    {
        add(t.commands.cut.text, can_copy && editable(), Command::Cut);
        add(t.commands.copy.text, can_copy, Command::Copy);
    }
    add(t.commands.paste.text, editable() && clipboard().text().has_value(), Command::Paste);
    if (editable() && (can_undo() || can_redo()))
    {
        separator();
        add(t.commands.undo.text, can_undo(), Command::Undo);
        add(t.commands.redo.text, can_redo(), Command::Redo);
    }
    separator();
    add(t.commands.select_all.text, !text_.empty(), Command::SelectAll);

    const std::optional<std::size_t> choice = host.show(anchor, items);
    if (!choice || *choice >= commands.size())
    {
        return true;
    }
    const auto [command, payload] = commands[*choice];
    switch (command)
    {
        case Command::Suggestion:
        {
            const std::string& replacement = suggestions[payload];
            edit(error->start, error->length, replacement);
            caret_            = error->start + replacement.size();
            selection_anchor_ = caret_;
            changed();
            break;
        }
        case Command::DeleteWord:
        {
            // Take the whitespace in front of the repeated word with it: "the the" → "the".
            std::size_t start = error->start;
            while (start > 0 && (text_[start - 1] == ' ' || text_[start - 1] == '\t'))
            {
                --start;
            }
            const std::size_t end = error->start + error->length;
            edit(start, end - start, {});
            caret_            = start;
            selection_anchor_ = caret_;
            changed();
            break;
        }
        case Command::AddToDictionary:
            spell_checker_->add_to_dictionary(word);
            forget_spelling(word);
            break;
        case Command::Ignore:
            spell_checker_->ignore_for_session(word);
            forget_spelling(word);
            break;
        case Command::Cut:
            copy_selection(true);
            break;
        case Command::Copy:
            copy_selection(false);
            break;
        case Command::Paste:
            paste();
            break;
        case Command::SelectAll:
            selection_anchor_ = 0;
            caret_            = text_.size();
            break;
        case Command::Undo:
            undo();
            break;
        case Command::Redo:
            redo();
            break;
        case Command::None:
            break;
    }
    return true;
}

void TextBoxControl::tick_override(Duration delta)
{
    // Runs focused or not: a debounced check must still land after focus moves on, and a
    // generation change from another field's menu must clear stale squiggles here too.
    if (spell_checker_ && spelling_on())
    {
        if (!spell_check_pending_ && spell_checker_->generation() != spell_generation_)
        {
            schedule_spell_check(0.0);
        }
        if (spell_check_pending_)
        {
            spell_check_delay_ms_ -= delta.count();
            if (spell_check_delay_ms_ <= 0.0)
            {
                run_spell_check();
            }
            else
            {
                request_tick_in(Duration{spell_check_delay_ms_}); // one wake when the typing pause is long enough
            }
        }
    }

    if (multiline())
    {
        for (ScrollBarOverlay* bar : {&bar_, &hbar_})
        {
            if (bar->update(delta))
            {
                mark_dirty(DirtyFlags::Visual);
            }
            if (const std::optional<Duration> wait = bar->fade_wait())
            {
                request_tick_in(*wait);
            }
        }
    }

    if (!is_focused() || !editable())
    {
        return;
    }
    // CaretBlinkIntervalMs is the system's default blink time; the live system setting is not read.
    caret_blink_ms_ += delta.count();
    if (caret_blink_ms_ >= CaretBlinkIntervalMs)
    {
        // Preserves phase rather than resetting to 0, and a long gap (a window restored) toggles once.
        caret_blink_ms_ = std::fmod(caret_blink_ms_, CaretBlinkIntervalMs);
        caret_visible_  = !caret_visible_;
        mark_dirty(DirtyFlags::Visual);
    }
    // The next blink is a timed step: the window sleeps until then.
    request_tick_in(Duration{CaretBlinkIntervalMs - caret_blink_ms_});
}

void TextBoxControl::draw_line_numbers(Renderer& renderer, std::size_t first, std::size_t last, const Color& text_color) const
{
    const Rect area = text_area();
    const Rect column{area.x - gutter_width_, area.y, gutter_width_ - LineNumberEnd, area.height};
    TextStyle  style = cached_style_;
    style.horizontal = TextAlign::Trailing;
    style.vertical   = TextAlign::Center;
    style.wrapping   = TextWrapping::NoWrap;
    // The caret's line in the text color, the others dimmed.
    const Color dim = is_enabled() ? text_color.with_alpha(0.45f) : text_color;

    const std::vector<TextLayout::Line>& lines = layout_.lines();
    // A line of the text starts each layout line after a hard break; its number counts those.
    std::size_t number = 1;
    for (std::size_t l = 0; l < first && l < lines.size(); ++l)
    {
        number += lines[l].hard_break ? 1 : 0;
    }
    const std::size_t caret_line = lines.empty() ? 0 : layout_.line_of(caret_);
    std::size_t       caret_from = caret_line; // the first layout line of the caret's line
    while (caret_from > 0 && !lines[caret_from - 1].hard_break)
    {
        --caret_from;
    }
    renderer.push_clip(Rect{column.x, area.y, gutter_width_, area.height});
    for (std::size_t l = first; l < last; ++l)
    {
        const bool starts = l == 0 || lines[l - 1].hard_break;
        if (starts)
        {
            const float y = area.y - scroll_y_ + lines[l].top;
            renderer.draw_text(std::to_string(number),
                               Rect{column.x, y, column.width, lines[l].height},
                               is_focused() && l == caret_from ? text_color : dim,
                               style);
        }
        number += lines[l].hard_break ? 1 : 0;
    }
    renderer.pop_clip();
}

void TextBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    description_.render(renderer, theme, is_enabled());

    if (!chromeless_)
    {
        detail::draw_text_control_chrome(renderer,
                                         theme,
                                         bounds(),
                                         is_enabled(),
                                         is_focused(),
                                         hovered_,
                                         description_.has_error() || invalid_border_);
    }

    const Rect text_rect   = text_area();
    TextStyle  style       = text_style(theme);
    style.horizontal       = TextAlign::Leading;
    const Color text_color = is_enabled() ? foreground_.resolve(theme).value_or(theme.colors().field_text)
                                          : theme.button_disabled.text;
    // The selection is the accent by default, with the selected text over it in the theme's
    // selection text.
    // Unfocused, the selection shows only in a color of the app's, with the text over it as is.
    const std::optional<Color> unfocused_fill = unfocused_selection_color_.resolve(theme);
    const Color selection_fill = is_focused() ? selection_color_.resolve(theme).value_or(theme.colors().selection)
                                              : unfocused_fill.value_or(Color{});
    const Color selected_text  = is_focused() ? theme.colors().selection_text : text_color;
    const bool  show_selection = has_selection() && (is_focused() || unfocused_fill.has_value());
    // The placeholder is the secondary text color, roughly 60% of the primary text; derived from
    // the field's own text color so every pack reads it.
    const Color hint = is_enabled() ? theme.colors().field_text.with_alpha(0.6f) : theme.button_disabled.text;

    if (multiline())
    {
        sync_layout();
        // Bring the caret into view after it or the text moved, once, so the wheel and the scroll
        // bars can take the view away from it; never scroll past the text.
        const float content  = layout_.size().height;
        const bool  sideways = wrapping_ == TextWrapping::NoWrap;
        if (follow_caret_ && !layout_.lines().empty())
        {
            follow_caret_    = false;
            const Rect caret = layout_.caret_rect(caret_);
            if (caret.bottom() > scroll_y_ + text_rect.height)
            {
                scroll_y_ = caret.bottom() - text_rect.height;
            }
            if (caret.top() < scroll_y_)
            {
                scroll_y_ = caret.top();
            }
            // Sideways, a little of the text past the caret stays in view.
            const float lead = std::min(text_rect.width / 4.0f, font_em_ * 4.0f);
            if (caret.x + 1.0f > scroll_x_ + text_rect.width)
            {
                scroll_x_ = caret.x + 1.0f - text_rect.width + lead;
            }
            if (caret.x < scroll_x_)
            {
                scroll_x_ = caret.x - lead;
            }
        }
        scroll_y_ = std::clamp(scroll_y_, 0.0f, std::max(0.0f, content - text_rect.height));
        scroll_x_ = std::clamp(scroll_x_, 0.0f, max_scroll_x());

        // Only the lines in view are drawn.
        const std::vector<TextLayout::Line>& lines = layout_.lines();
        const std::size_t                    first = static_cast<std::size_t>(
            std::ranges::partition_point(lines, [&](const TextLayout::Line& l) { return l.top + l.height <= scroll_y_; }) -
            lines.begin());
        const std::size_t
            last = std::max(first,
                            static_cast<std::size_t>(
                                std::ranges::partition_point(lines,
                                                             [&](const TextLayout::Line& l)
                                                             { return l.top < scroll_y_ + text_rect.height; }) -
                                lines.begin()));
        if (gutter_width_ > 0.0f)
        {
            draw_line_numbers(renderer, first, last, text_color);
        }

        renderer.push_clip(text_rect);
        const Point origin = text_origin();
        if (text_.empty() && !placeholder_text_.empty())
        {
            TextStyle hint_style = style;
            hint_style.vertical  = TextAlign::Leading;
            hint_style.wrapping  = wrapping_;
            renderer.draw_text(placeholder_text_, text_rect, hint, hint_style);
        }
        else if (first < last)
        {
            TextLayout::Paint paint;
            paint.foreground = text_color;
            paint.first_line = first;
            paint.last_line  = last;
            if (is_enabled())
            {
                paint.colors = color_spans_;
            }
            if (show_selection)
            {
                const auto [start, end] = selection_range();
                paint.selection         = TextHighlight{start, end, selection_fill, selected_text};
            }
            layout_.render(renderer, Point{origin.x, origin.y + lines[first].top}, paint);
        }
        if (is_enabled())
        {
            for (const SpellingError& error : spelling_errors_)
            {
                if (!spelling_shown(error))
                {
                    continue;
                }
                for (const Rect& r : layout_.range_rects(error.start, error.start + error.length))
                {
                    const float y = origin.y + r.bottom() - 1.5f;
                    renderer.stroke_wavy_line(Point{origin.x + r.left(), y},
                                              Point{origin.x + r.right(), y},
                                              theme.spelling_error,
                                              SquiggleStrokeWidth,
                                              SquiggleAmplitude,
                                              SquiggleWavelength);
                }
            }
        }
        if (is_focused() && editable() && caret_visible_ && !layout_.lines().empty())
        {
            const Rect caret = layout_.caret_rect(caret_);
            renderer.stroke_line(Point{origin.x + caret.x, origin.y + caret.top()},
                                 Point{origin.x + caret.x, origin.y + caret.bottom()},
                                 text_color,
                                 1.0f);
        }
        renderer.pop_clip();
        // Draw nothing while the text fits.
        bar_.render(renderer, theme, bar_viewport(), content, scroll_y_);
        if (sideways)
        {
            hbar_.render(renderer, theme, hbar_viewport(), layout_.size().width + 1.0f, scroll_x_);
        }
        return;
    }

    Rect selection_rect{};
    if (show_selection)
    {
        const auto [start, end] = selection_range();
        const float x0          = text_rect.left() + prefix_width(start, style);
        const float x1          = text_rect.left() + prefix_width(end, style);
        selection_rect          = Rect{x0, text_rect.top(), x1 - x0, text_rect.height};
        renderer.fill_rounded_rect(selection_rect, 0.0f, selection_fill);
    }

    if (text_.empty() && !placeholder_text_.empty())
    {
        renderer.draw_text(placeholder_text_, text_rect, hint, style);
    }
    else
    {
        const std::string shown = display_text();
        renderer.draw_text(shown, text_rect, text_color, style);
        if (show_selection && selection_rect.width > 0.0f)
        {
            renderer.push_clip(selection_rect);
            renderer.draw_text(shown, text_rect, selected_text, style);
            renderer.pop_clip();
        }
    }

    // Misspelling squiggles, along the bottom of the (vertically centered) line of text,
    // clipped to the text area: a word scrolled past the right edge gets none.
    if (is_enabled() && !spelling_errors_.empty())
    {
        const float line_height = line_height_ > 0.0f ? line_height_ : text_rect.height;
        const float y           = text_rect.top() + (text_rect.height + line_height) * 0.5f - 1.5f;
        for (const SpellingError& error : spelling_errors_)
        {
            if (!spelling_shown(error))
            {
                continue;
            }
            const float x0 = text_rect.left() + prefix_width(error.start, style);
            const float x1 = std::min(text_rect.right(), text_rect.left() + prefix_width(error.start + error.length, style));
            if (x1 <= x0)
            {
                continue;
            }
            renderer.stroke_wavy_line(Point{x0, y},
                                      Point{x1, y},
                                      theme.spelling_error,
                                      SquiggleStrokeWidth,
                                      SquiggleAmplitude,
                                      SquiggleWavelength);
        }
    }

    if (is_focused() && editable() && caret_visible_)
    {
        // One line tall, centered like the text: a box stretched taller than its text keeps a
        // caret the size of the font.
        const float caret_x     = text_rect.left() + prefix_width(caret_, style);
        const float line_height = line_height_ > 0.0f ? std::min(line_height_, text_rect.height) : text_rect.height;
        const float caret_top   = text_rect.top() + (text_rect.height - line_height) * 0.5f;
        renderer.stroke_line(Point{caret_x, caret_top}, Point{caret_x, caret_top + line_height}, text_color, 1.0f);
    }

    if (delete_button_visible())
    {
        // The subtle ramp behind a secondary-text glyph, the tertiary text color while pressed.
        const ButtonState state  = delete_pressed_   ? ButtonState::Pressed
                                   : delete_hovered_ ? ButtonState::Hover
                                                     : ButtonState::Normal;
        const Rect        button = delete_button_rect().inset(DeleteButtonMargin);
        const Color       fill   = theme.subtle_for_state(state).background;
        if (fill.a > 0.0f)
        {
            renderer.fill_rounded_rect(button, theme.corner_radius, fill);
        }
        const Color glyph = theme.colors().field_text.with_alpha(delete_pressed_ ? 0.45f : 0.6f);
        draw_icon(renderer, DeleteGlyph, button, glyph, 0.0f, theme.icon_font_family);
    }
}

bool TextBoxControl::wheel_override(const PointerEvent& e)
{
    if (!multiline() || !is_enabled() || !bounds().contains(e.position))
    {
        return false;
    }
    sync_layout();
    // A horizontal wheel, or Shift with a vertical one, scrolls a NoWrap box sideways.
    if (e.horizontal_wheel || e.modifiers.shift)
    {
        const float before = scroll_x_;
        scroll_x_          = std::clamp(scroll_x_ + e.wheel_delta, 0.0f, max_scroll_x());
        if (scroll_x_ == before)
        {
            return false;
        }
        hbar_.notify_activity();
        mark_dirty(DirtyFlags::Visual);
        return true;
    }
    const float view = text_area().height;
    const float max  = std::max(0.0f, layout_.size().height - view);
    if (max <= 0.0f)
    {
        return false;
    }
    const float before = scroll_y_;
    scroll_y_          = std::clamp(scroll_y_ + e.wheel_delta, 0.0f, max);
    if (scroll_y_ != before)
    {
        bar_.notify_activity();
        mark_dirty(DirtyFlags::Visual);
        return true;
    }
    return false;
}

std::optional<CursorShape> TextBoxControl::cursor_at(Point p) const
{
    // The IBeam covers the text column, padding included; the delete button (and whatever
    // a containing control keeps in the trailing space: a reveal, query or spin button) is Arrow.
    const float buttons = trailing_space_ + (delete_button_visible() ? DeleteButtonWidth : 0.0f);
    if (multiline() && bar_.scrollable(bar_viewport(), layout_.size().height) &&
        bar_.band_bounds(bar_viewport()).contains(p))
    {
        return CursorShape::Arrow; // the scroll bar
    }
    if (multiline() && wrapping_ == TextWrapping::NoWrap &&
        hbar_.scrollable(hbar_viewport(), layout_.size().width + 1.0f) && hbar_.band_bounds(hbar_viewport()).contains(p))
    {
        return CursorShape::Arrow;
    }
    return p.x < bounds().right() - buttons ? CursorShape::IBeam : CursorShape::Arrow;
}

std::unique_ptr<ui::Element> TextBox::build() &&
{
    auto node = std::make_unique<ui::TextBoxControl>();
    apply(header, *node, [raw = node.get()](const std::string& value) { raw->set_header(value); });
    apply(description, *node, [raw = node.get()](const std::string& value) { raw->set_description(value); });
    apply(error, *node, [raw = node.get()](const std::string& value) { raw->set_error(value); });

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(spell_check, *node, [raw = node.get()](const bool& v) { raw->set_spell_check(v); });
    apply(placeholder_text, *node, [raw = node.get()](const std::string& value) { raw->set_placeholder_text(value); });
    apply(read_only, *node, [raw = node.get()](const bool& value) { raw->set_read_only(value); });
    apply(accepts_return, *node, [raw = node.get()](const bool& v) { raw->set_accepts_return(v); });
    apply(accepts_tab, *node, [raw = node.get()](const bool& v) { raw->set_accepts_tab(v); });
    apply(wrapping, *node, [raw = node.get()](const TextWrapping& v) { raw->set_wrapping(v); });
    if (font_family.reactive() || font_family.initial())
    {
        apply(font_family, *node, [raw = node.get()](const std::optional<std::string>& v) { raw->set_font_family(v); });
    }
    if (font_size.reactive() || font_size.initial())
    {
        apply(font_size, *node, [raw = node.get()](const std::optional<float>& v) { raw->set_font_size(v); });
    }
    apply(font_weight, *node, [raw = node.get()](const std::optional<FontWeight>& v) { raw->set_font_weight(v); });
    apply(italic, *node, [raw = node.get()](const bool& v) { raw->set_italic(v); });
    apply(character_spacing, *node, [raw = node.get()](const int& v) { raw->set_character_spacing(v); });
    apply(foreground, *node, [raw = node.get()](const Paint& v) { raw->set_foreground(v); });
    apply(selection_highlight_color,
          *node,
          [raw = node.get()](const Paint& v)
          { ui::detail::set_paint(v, [raw](auto c) { raw->set_selection_highlight_color(c); }); });
    apply(unfocused_selection_highlight_color,
          *node,
          [raw = node.get()](const Paint& v)
          { ui::detail::set_paint(v, [raw](auto c) { raw->set_unfocused_selection_highlight_color(c); }); });
    apply(color_spans, *node, [raw = node.get()](const std::vector<TextColorSpan>& v) { raw->set_color_spans(v); });
    apply(line_numbers, *node, [raw = node.get()](const bool& v) { raw->set_line_numbers(v); });
    apply(max_length, *node, [raw = node.get()](const std::size_t& value) { raw->set_max_length(value); });
    apply(character_casing,
          *node,
          [raw = node.get()](const ui::CharacterCasing& value) { raw->set_character_casing(value); });

    if (auto changed = ui::write_back(text, std::move(on_text_changed)))
    {
        node->on_text_changed(std::move(changed));
    }
    if (on_edit)
    {
        node->on_edit(std::move(on_edit));
    }
    if (on_submit)
    {
        node->on_submit(std::move(on_submit));
    }
    if (on_selection_changed)
    {
        node->on_selection_changed(std::move(on_selection_changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui

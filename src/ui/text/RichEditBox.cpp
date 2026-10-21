// SPDX-License-Identifier: MIT
#include "winui/ui/text/RichEditBox.hpp"

#include "../../platform/Backend.hpp"
#include "Rtf.hpp"
#include "TextControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/ContextMenuHost.hpp"
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
#include <functional>
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

namespace
{
// The text field's minimum width, as TextBox's.
constexpr float RichEditBoxMinWidth = 64.0f;

bool is_space(char32_t ch)
{
    return (ch >= 0x09 && ch <= 0x0D) || ch == ' ' || ch == 0x85 || ch == 0xA0 || ch == 0x1680 ||
           (ch >= 0x2000 && ch <= 0x200A) || ch == 0x2028 || ch == 0x2029 || ch == 0x202F || ch == 0x205F || ch == 0x3000;
}

// Drops control characters but a tab (and a line break when `keep_breaks`). Byte by byte is safe:
// no byte of a multi-byte UTF-8 sequence is below 0x80.
std::string strip_controls(std::string_view text, bool keep_breaks)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\t' || (c == '\n' && keep_breaks))
        {
            out += c;
        }
    }
    return out;
}

// Line ends as the document keeps them: \r\n and \r become \n.
std::string normalize_breaks(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                ++i;
            }
            out += '\n';
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

RichEditBoxControl::CharacterFormat to_character_format(const RichTextFormat& f)
{
    const auto unset = [](const auto& field, RichTextFormatField flag)
    { return field ? RichTextFormatField::None : flag; };
    return RichEditBoxControl::CharacterFormat{
        .bold            = f.bold,
        .italic          = f.italic,
        .underline       = f.underline,
        .strikethrough   = f.strikethrough,
        .foreground      = f.foreground,
        .background      = f.background,
        .font_family     = f.font_family,
        .font_size       = f.font_size,
        .control_default = unset(f.foreground, RichTextFormatField::Foreground) |
                           unset(f.background, RichTextFormatField::Background) |
                           unset(f.font_family, RichTextFormatField::FontFamily) |
                           unset(f.font_size, RichTextFormatField::FontSize),
    };
}

RichTextFormat merged(RichTextFormat f, const RichEditBoxControl::CharacterFormat& request)
{
    if (has_flag(request.control_default, RichTextFormatField::Foreground))
    {
        f.foreground.reset();
    }
    if (has_flag(request.control_default, RichTextFormatField::Background))
    {
        f.background.reset();
    }
    if (has_flag(request.control_default, RichTextFormatField::FontFamily))
    {
        f.font_family.reset();
    }
    if (has_flag(request.control_default, RichTextFormatField::FontSize))
    {
        f.font_size.reset();
    }
    if (request.bold)
    {
        f.bold = *request.bold;
    }
    if (request.italic)
    {
        f.italic = *request.italic;
    }
    if (request.underline)
    {
        f.underline = *request.underline;
    }
    if (request.strikethrough)
    {
        f.strikethrough = *request.strikethrough;
    }
    if (request.foreground)
    {
        f.foreground = request.foreground;
    }
    if (request.background)
    {
        f.background = request.background;
    }
    if (request.font_family)
    {
        f.font_family = request.font_family;
    }
    if (request.font_size)
    {
        f.font_size = std::max(0.0f, *request.font_size);
    }
    return f;
}

void intersect(RichEditBoxControl::CharacterFormat& shared, const RichEditBoxControl::CharacterFormat& f)
{
    const auto keep = [](auto& a, const auto& b)
    {
        if (a && !(a == b))
        {
            a.reset();
        }
    };
    keep(shared.bold, f.bold);
    keep(shared.italic, f.italic);
    keep(shared.underline, f.underline);
    keep(shared.strikethrough, f.strikethrough);
    keep(shared.foreground, f.foreground);
    keep(shared.background, f.background);
    keep(shared.font_family, f.font_family);
    keep(shared.font_size, f.font_size);
    shared.control_default = shared.control_default & f.control_default;
}

// What this process last copied out of a RichEditBox, so pasting it back keeps its formatting
// (the clipboard itself only carries the plain text).
struct RichClip
{
    std::string               plain;
    std::vector<RichTextSpan> spans;
};
RichClip& rich_clip()
{
    static RichClip clip;
    return clip;
}
} // namespace

RichEditBoxControl::RichEditBoxControl()
{
    detail::link_spell_checker(); // the OS checker is linked only into an app with a text field
    set_min_width(RichEditBoxMinWidth);
}

RichEditBoxControl::RichEditBoxControl(std::string text) : RichEditBoxControl()
{
    set_text(std::move(text));
}

// ---- the document ----------------------------------------------------------------

void RichEditBoxControl::normalize_runs()
{
    std::vector<Run> out;
    out.reserve(runs_.size());
    for (Run& run : runs_)
    {
        if (run.length == 0)
        {
            continue;
        }
        if (!out.empty() && out.back().format == run.format)
        {
            out.back().length += run.length;
        }
        else
        {
            out.push_back(std::move(run));
        }
    }
    runs_ = std::move(out);
}

RichTextFormat RichEditBoxControl::format_at(std::size_t index) const
{
    std::size_t start = 0;
    for (const Run& run : runs_)
    {
        if (index < start + run.length)
        {
            return run.format;
        }
        start += run.length;
    }
    return runs_.empty() ? RichTextFormat{} : runs_.back().format;
}

RichTextFormat RichEditBoxControl::typing_format() const
{
    if (pending_format_)
    {
        return *pending_format_;
    }
    // Typing at a paragraph's start takes the paragraph's first character's format.
    if (caret_ > 0 && text_[caret_ - 1] != '\n')
    {
        return format_at(caret_ - 1);
    }
    if (caret_ < text_.size())
    {
        return format_at(caret_);
    }
    return caret_ > 0 ? format_at(caret_ - 1) : RichTextFormat{};
}

namespace
{
// Splits the run holding `pos` so a run starts there; returns that run's index (runs.size() at
// the end of the text).
template <typename Run>
std::size_t split_runs(std::vector<Run>& runs, std::size_t pos)
{
    std::size_t start = 0;
    for (std::size_t i = 0; i < runs.size(); ++i)
    {
        if (pos == start)
        {
            return i;
        }
        if (pos < start + runs[i].length)
        {
            Run tail = runs[i];
            tail.length -= pos - start;
            runs[i].length = pos - start;
            runs.insert(runs.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(tail));
            return i + 1;
        }
        start += runs[i].length;
    }
    return runs.size();
}
} // namespace

void RichEditBoxControl::replace(std::size_t start, std::size_t end, std::string_view text, const RichTextFormat& format)
{
    end                     = std::min(end, text_.size());
    start                   = std::min(start, end);
    const std::size_t first = split_runs(runs_, start);
    const std::size_t last  = split_runs(runs_, end);
    runs_.erase(runs_.begin() + static_cast<std::ptrdiff_t>(first), runs_.begin() + static_cast<std::ptrdiff_t>(last));
    if (!text.empty())
    {
        runs_.insert(runs_.begin() + static_cast<std::ptrdiff_t>(first), Run{text.size(), format});
    }
    text_.replace(start, end - start, text);
    normalize_runs();
    note_edit(start, end - start, text.size());
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichEditBoxControl::insert_runs(std::size_t at, const std::vector<RichTextSpan>& spans)
{
    at                  = std::min(at, text_.size());
    std::size_t index   = split_runs(runs_, at);
    std::size_t written = 0;
    std::string inserted;
    for (const RichTextSpan& span : spans)
    {
        if (span.text.empty())
        {
            continue;
        }
        runs_.insert(runs_.begin() + static_cast<std::ptrdiff_t>(index++), Run{span.text.size(), span.format});
        inserted += span.text;
        written += span.text.size();
    }
    text_.insert(at, inserted);
    normalize_runs();
    note_edit(at, 0, written);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichEditBoxControl::apply_format(std::size_t start, std::size_t end, const CharacterFormat& format)
{
    end   = std::min(end, text_.size());
    start = std::min(start, end);
    if (start == end)
    {
        return;
    }
    const std::size_t first = split_runs(runs_, start);
    const std::size_t last  = split_runs(runs_, end);
    for (std::size_t i = first; i < last; ++i)
    {
        runs_[i].format = merged(runs_[i].format, format);
    }
    normalize_runs();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichEditBoxControl::reset_document(std::string text, std::vector<Run> runs)
{
    text_ = std::move(text);
    runs_ = std::move(runs);
    normalize_runs();
    caret_  = text_.size();
    anchor_ = caret_;
    pending_format_.reset();
    preferred_x_.reset();
    undo_.clear();
    redo_.clear();
    last_edit_ = EditKind::Other;
    scroll_y_  = 0.0f;
    spelling_errors_.clear();
    if (spell_checker_ && spelling_on())
    {
        spell_check_pending_  = true;
        spell_check_delay_ms_ = 0.0;
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichEditBoxControl::set_text(std::string text)
{
    text                   = normalize_breaks(text);
    const std::size_t size = text.size();
    reset_document(std::move(text), {Run{size, {}}});
}

void RichEditBoxControl::set_spans(const std::vector<RichTextSpan>& spans)
{
    std::string      text;
    std::vector<Run> runs;
    for (const RichTextSpan& span : spans)
    {
        const std::string piece = normalize_breaks(span.text);
        text += piece;
        runs.push_back(Run{piece.size(), span.format});
    }
    reset_document(std::move(text), std::move(runs));
}

std::vector<RichTextSpan> RichEditBoxControl::spans_in(std::size_t start, std::size_t end) const
{
    std::vector<RichTextSpan> out;
    std::size_t               pos = 0;
    for (const Run& run : runs_)
    {
        const std::size_t run_end = pos + run.length;
        const std::size_t a       = std::max(pos, start);
        const std::size_t b       = std::min(run_end, end);
        if (a < b)
        {
            out.push_back(RichTextSpan{text_.substr(a, b - a), run.format});
        }
        pos = run_end;
    }
    return out;
}

std::vector<RichTextSpan> RichEditBoxControl::spans() const
{
    return spans_in(0, text_.size());
}

RichTextFormat RichEditBoxControl::unspecified(RichTextFormat format) const
{
    if (format.font_family == base_style_.font_family.view())
    {
        format.font_family.reset();
    }
    if (format.font_size && std::abs(*format.font_size - base_style_.font_size) < 0.5f)
    {
        format.font_size.reset();
    }
    return format;
}

void RichEditBoxControl::load_rtf(std::string_view rtf)
{
    bool                      is_rtf = false;
    std::vector<RichTextSpan> spans  = detail::parse_rtf(rtf, &is_rtf);
    if (!is_rtf)
    {
        // Not RTF: the bytes as UTF-8 text, a malformed sequence shown as U+FFFD.
        set_text(sanitize_utf8(rtf));
        return;
    }
    for (RichTextSpan& span : spans)
    {
        span.format = unspecified(span.format);
    }
    set_spans(spans);
}

std::string RichEditBoxControl::save_rtf() const
{
    return detail::write_rtf(spans(), base_style_.font_family.str(), base_style_.font_size);
}

// ---- selection and formatting -------------------------------------------------------

std::pair<std::size_t, std::size_t> RichEditBoxControl::selection() const
{
    return {std::min(caret_, anchor_), std::max(caret_, anchor_)};
}

void RichEditBoxControl::select(std::size_t start, std::size_t end)
{
    anchor_ = std::min(start, text_.size());
    caret_  = std::min(end, text_.size());
    pending_format_.reset();
    preferred_x_.reset();
    last_edit_ = EditKind::Other;
    mark_dirty(DirtyFlags::Visual);
}

void RichEditBoxControl::select_all()
{
    select(0, text_.size());
}

std::string RichEditBoxControl::selected_text() const
{
    const auto [start, end] = selection();
    return text_.substr(start, end - start);
}

RichEditBoxControl::CharacterFormat RichEditBoxControl::range_format(std::size_t start, std::size_t end) const
{
    end   = std::min(end, text_.size());
    start = std::min(start, end);
    if (start == end)
    {
        return to_character_format(format_at(start));
    }
    std::optional<CharacterFormat> shared;
    std::size_t                    pos = 0;
    for (const Run& run : runs_)
    {
        const std::size_t run_end = pos + run.length;
        if (run_end > start && pos < end)
        {
            const CharacterFormat f = to_character_format(run.format);
            if (!shared)
            {
                shared = f;
            }
            else
            {
                intersect(*shared, f);
            }
        }
        pos = run_end;
    }
    return shared.value_or(CharacterFormat{});
}

RichEditBoxControl::CharacterFormat RichEditBoxControl::selection_format() const
{
    if (!has_selection())
    {
        return to_character_format(typing_format());
    }
    const auto [start, end] = selection();
    return range_format(start, end);
}

void RichEditBoxControl::set_selection_format(const CharacterFormat& format)
{
    if (!has_selection())
    {
        pending_format_ = merged(typing_format(), format);
        return;
    }
    const auto [start, end] = selection();
    std::vector<Run> before = runs_;
    apply_format(start, end, format);
    if (runs_ == before)
    {
        return; // only an actual change is an undo step
    }
    std::vector<Run> after = std::move(runs_);
    runs_                  = std::move(before);
    begin_edit(EditKind::Other);
    runs_      = std::move(after);
    last_edit_ = EditKind::Other;
}

void RichEditBoxControl::set_range_format(std::size_t start, std::size_t end, const CharacterFormat& format)
{
    apply_format(start, end, format);
}

void RichEditBoxControl::toggle(std::optional<bool> CharacterFormat::* field)
{
    CharacterFormat request;
    request.*field = !((selection_format().*field).value_or(false));
    set_selection_format(request);
}

void RichEditBoxControl::toggle_bold()
{
    toggle(&CharacterFormat::bold);
}

void RichEditBoxControl::toggle_italic()
{
    toggle(&CharacterFormat::italic);
}

void RichEditBoxControl::toggle_underline()
{
    toggle(&CharacterFormat::underline);
}

std::vector<std::pair<std::size_t, std::size_t>> RichEditBoxControl::find_all(std::string_view needle) const
{
    std::vector<std::pair<std::size_t, std::size_t>> found;
    if (needle.empty() || needle.size() > text_.size())
    {
        return found;
    }
    // Case-insensitive per code point; the match ends wherever the text's code points do.
    const TextAnalyzer& analyzer = text_analyzer();
    const auto          fold     = [&](char32_t cp)
    {
        return cp < 0x80 ? std::string(1, static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 0x20 : cp))
                         : analyzer.to_lower(encode_utf8(cp));
    };
    const auto match = [&](std::size_t at) -> std::size_t
    {
        std::size_t t = at;
        for (std::size_t n = 0; n < needle.size(); n = next_code_point(needle, n))
        {
            if (t >= text_.size() || fold(code_point_at(text_, t)) != fold(code_point_at(needle, n)))
            {
                return 0;
            }
            t = next_code_point(text_, t);
        }
        return t;
    };
    for (std::size_t i = 0; i < text_.size();)
    {
        if (const std::size_t end = match(i); end > i)
        {
            found.emplace_back(i, end);
            i = end;
        }
        else
        {
            i = next_code_point(text_, i);
        }
    }
    return found;
}

// ---- editing --------------------------------------------------------------------------

RichEditBoxControl::Snapshot RichEditBoxControl::snapshot() const
{
    return Snapshot{text_, runs_, anchor_, caret_};
}

void RichEditBoxControl::restore(const Snapshot& snapshot)
{
    text_   = snapshot.text;
    runs_   = snapshot.runs;
    anchor_ = std::min(snapshot.anchor, text_.size());
    caret_  = std::min(snapshot.caret, text_.size());
    pending_format_.reset();
    preferred_x_.reset();
    spelling_errors_.clear();
    if (spell_checker_ && spelling_on())
    {
        spell_check_pending_  = true;
        spell_check_delay_ms_ = 0.0;
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichEditBoxControl::begin_edit(EditKind kind)
{
    // Typing (or deleting) carries on the step already open until the caret moves.
    if (kind != EditKind::Other && kind == last_edit_ && !undo_.empty())
    {
        return;
    }
    undo_.push_back(snapshot());
    if (undo_.size() > UndoLimit)
    {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    last_edit_ = kind;
}

void RichEditBoxControl::undo()
{
    if (undo_.empty())
    {
        return;
    }
    redo_.push_back(snapshot());
    restore(undo_.back());
    undo_.pop_back();
    last_edit_ = EditKind::Other;
}

void RichEditBoxControl::redo()
{
    if (redo_.empty())
    {
        return;
    }
    undo_.push_back(snapshot());
    restore(redo_.back());
    redo_.pop_back();
    last_edit_ = EditKind::Other;
}

std::string RichEditBoxControl::admit(std::string_view text) const
{
    const TextAnalyzer& analyzer = text_analyzer();
    std::string         out      = casing_ == CharacterCasing::Upper   ? analyzer.to_upper(text)
                                   : casing_ == CharacterCasing::Lower ? analyzer.to_lower(text)
                                                                       : std::string{text};
    if (max_length_ > 0)
    {
        const auto [start, end] = selection();
        const std::size_t kept  = code_point_count(text_) -
                                  code_point_count(std::string_view{text_}.substr(start, end - start));
        const std::size_t limit = max_length_;
        const std::size_t room  = kept < limit ? limit - kept : 0;
        std::size_t       cut   = 0;
        std::size_t       count = 0;
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

void RichEditBoxControl::user_changed()
{
    reset_caret_blink();
    if (on_text_changed_)
    {
        on_text_changed_(text_);
    }
}

void RichEditBoxControl::type_text(std::string_view text)
{
    if (!editable())
    {
        return;
    }
    const std::string admitted = admit(strip_controls(normalize_breaks(text), accepts_return_));
    if (admitted.empty() && !has_selection())
    {
        return;
    }
    const auto [start, end]     = selection();
    const RichTextFormat format = has_selection() ? format_at(start) : typing_format();
    begin_edit(EditKind::Other);
    replace(start, end, admitted, format);
    caret_  = start + admitted.size();
    anchor_ = caret_;
    pending_format_.reset();
    last_edit_ = EditKind::Other;
    user_changed();
}

void RichEditBoxControl::erase_selection()
{
    const auto [start, end] = selection();
    begin_edit(EditKind::Other);
    replace(start, end, "", {});
    caret_     = start;
    anchor_    = start;
    last_edit_ = EditKind::Other;
}

void RichEditBoxControl::delete_backward(bool word)
{
    if (has_selection())
    {
        erase_selection();
        user_changed();
        return;
    }
    if (caret_ == 0)
    {
        return;
    }
    const std::size_t    from = word ? word_left(caret_) : text_analyzer().previous_cluster(text_, caret_);
    const RichTextFormat kept = format_at(from); // what typing on picks up, as RichEdit keeps it
    begin_edit(EditKind::Deleting);
    replace(from, caret_, "", {});
    caret_  = from;
    anchor_ = from;
    if (from == text_.size() || (from < text_.size() && text_[from] == '\n'))
    {
        pending_format_ = kept;
    }
    user_changed();
}

void RichEditBoxControl::delete_forward(bool word)
{
    if (has_selection())
    {
        erase_selection();
        user_changed();
        return;
    }
    if (caret_ >= text_.size())
    {
        return;
    }
    const std::size_t to = word ? word_right(caret_) : text_analyzer().next_cluster(text_, caret_);
    begin_edit(EditKind::Deleting);
    replace(caret_, to, "", {});
    anchor_ = caret_;
    user_changed();
}

void RichEditBoxControl::copy_selection(bool cut)
{
    if (!has_selection() || (cut && !editable()))
    {
        return;
    }
    const auto [start, end] = selection();
    RichClip& clip          = rich_clip();
    clip.plain              = text_.substr(start, end - start);
    clip.spans              = spans_in(start, end);
    clipboard().set_text(clip.plain);
    if (cut)
    {
        erase_selection();
        user_changed();
    }
}

void RichEditBoxControl::paste()
{
    if (!editable())
    {
        return;
    }
    const std::optional<std::string> clip = clipboard().text();
    if (!clip)
    {
        return;
    }
    const std::string normalized = normalize_breaks(*clip);
    const std::string admitted   = admit(strip_controls(normalized, accepts_return_));
    if (admitted.empty() && !has_selection())
    {
        return;
    }

    const auto [start, end]     = selection();
    const RichTextFormat format = has_selection() ? format_at(start) : typing_format();
    begin_edit(EditKind::Other);
    replace(start, end, "", {});
    // Text this process copied out of a RichEditBox comes back formatted, as long as nothing
    // about it had to change on the way in.
    const RichClip& rich = rich_clip();
    if (admitted == normalized && admitted == rich.plain && !rich.spans.empty())
    {
        insert_runs(start, rich.spans);
    }
    else
    {
        replace(start, start, admitted, format);
    }
    caret_  = start + admitted.size();
    anchor_ = caret_;
    pending_format_.reset();
    last_edit_ = EditKind::Other;
    user_changed();
}

bool RichEditBoxControl::set_text_value(std::string_view value)
{
    if (!editable())
    {
        return false;
    }
    select_all();
    type_text(value);
    return true;
}

void RichEditBoxControl::add_context_menu_item(std::string text, Action on_click)
{
    extra_menu_items_.push_back(ExtraMenuItem{std::move(text), std::move(on_click)});
}

// ---- caret movement ---------------------------------------------------------------------

void RichEditBoxControl::move_caret(std::size_t to, bool extend)
{
    to = std::min(to, text_.size());
    // What is typed next keeps a format picked for it until the caret really goes somewhere else:
    // a click back into the box where the caret already was (after a toolbar button) is not a move.
    if (to != caret_ || (!extend && anchor_ != to))
    {
        pending_format_.reset();
    }
    caret_ = to;
    if (!extend)
    {
        anchor_ = caret_;
    }
    last_edit_ = EditKind::Other;
}

std::size_t RichEditBoxControl::word_left(std::size_t from) const
{
    return previous_word_start(text_, from);
}

std::size_t RichEditBoxControl::word_right(std::size_t from) const
{
    return next_word_start(text_, from);
}

std::pair<std::size_t, std::size_t> RichEditBoxControl::word_range(std::size_t index) const
{
    if (text_.empty())
    {
        return {0, 0};
    }
    return word_run(text_, index, true);
}

void RichEditBoxControl::move_vertically(int lines, bool extend)
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
    std::size_t       to    = 0;
    if ((lines < 0 && line == 0) || (lines > 0 && line == last))
    {
        to = lines < 0 ? 0 : text_.size(); // past the first or last line, as a Win32 edit does
    }
    else
    {
        const std::ptrdiff_t target = std::clamp(static_cast<std::ptrdiff_t>(line) + lines,
                                                 std::ptrdiff_t{0},
                                                 static_cast<std::ptrdiff_t>(last));
        to                          = layout_.offset_at_x(static_cast<std::size_t>(target), x);
    }
    move_caret(to, extend);
    preferred_x_ = x;
}

void RichEditBoxControl::reset_caret_blink()
{
    caret_visible_  = true;
    caret_blink_ms_ = 0.0;
}

void RichEditBoxControl::notify_selection(std::pair<std::size_t, std::size_t> before)
{
    if (selection() != before)
    {
        mark_dirty(DirtyFlags::Visual);
        if (on_selection_changed_)
        {
            on_selection_changed_();
        }
    }
}

// ---- state ------------------------------------------------------------------------------

void RichEditBoxControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void RichEditBoxControl::set_read_only(bool read_only)
{
    if (read_only_ == read_only)
    {
        return;
    }
    read_only_ = read_only;
    spelling_errors_.clear();
    spell_check_pending_  = !read_only_ && spell_checker_ && spell_check_enabled_;
    spell_check_delay_ms_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
}

void RichEditBoxControl::focus_changed(bool /*focused*/)
{
    if (is_focused())
    {
        reset_caret_blink();
    }
    else
    {
        last_edit_ = EditKind::Other;
    }
    mark_dirty(DirtyFlags::Visual);
}

// ---- spelling --------------------------------------------------------------------------

void RichEditBoxControl::set_spell_check(bool enabled)
{
    if (enabled == spell_check_enabled_)
    {
        return;
    }
    spell_check_enabled_ = enabled;
    spelling_errors_.clear();
    spell_check_pending_  = enabled && spell_checker_ && spelling_on();
    spell_check_delay_ms_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
}

void RichEditBoxControl::sync_spell_checker(SpellChecker& checker)
{
    if (spell_checker_ == &checker)
    {
        return;
    }
    spell_checker_ = &checker;
    spelling_errors_.clear();
    if (spelling_on())
    {
        spell_check_pending_  = true;
        spell_check_delay_ms_ = 0.0;
    }
}

void RichEditBoxControl::note_edit(std::size_t pos, std::size_t removed, std::size_t inserted)
{
    const std::size_t removed_end = pos + removed;
    std::erase_if(spelling_errors_,
                  [&](const SpellingError& e) { return e.start + e.length >= pos && e.start <= removed_end; });
    for (SpellingError& e : spelling_errors_)
    {
        if (e.start > removed_end)
        {
            e.start = e.start - removed + inserted;
        }
    }
    if (spell_checker_ && spelling_on())
    {
        spell_check_pending_  = true;
        spell_check_delay_ms_ = SpellCheckDelayMs;
    }
}

void RichEditBoxControl::run_spell_check()
{
    spell_check_pending_ = false;
    spelling_errors_.clear();
    if (spell_checker_)
    {
        spell_generation_ = spell_checker_->generation();
        if (spelling_on() && !text_.empty() && spell_checker_->highlight_enabled() && spell_checker_->available())
        {
            spelling_errors_ = spell_checker_->check(text_);
        }
    }
    mark_dirty(DirtyFlags::Visual);
}

const SpellingError* RichEditBoxControl::spelling_error_at(std::size_t index) const
{
    for (const SpellingError& error : spelling_errors_)
    {
        if (error.start <= index && index <= error.start + error.length)
        {
            return &error;
        }
    }
    return nullptr;
}

// ---- layout ------------------------------------------------------------------------------

TextStyle RichEditBoxControl::base_style(const Theme& theme) const
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
    s.horizontal = TextAlign::Leading;
    return s;
}

std::vector<TextLayoutParagraph> RichEditBoxControl::layout_content() const
{
    TextLayoutParagraph paragraph;
    std::size_t         pos = 0;
    for (const Run& run : runs_)
    {
        TextStyle             style = base_style_;
        const RichTextFormat& f     = run.format;
        if (f.font_family)
        {
            style.font_family = *f.font_family;
        }
        if (f.font_size)
        {
            style.font_size = *f.font_size;
        }
        if (f.bold)
        {
            style.weight = FontWeight::Bold;
        }
        style.italic        = style.italic || f.italic;
        style.underline     = f.underline;
        style.strikethrough = f.strikethrough;
        paragraph.runs.push_back(TextLayoutRun{text_.substr(pos, run.length), style, f.foreground});
        pos += run.length;
    }
    if (paragraph.runs.empty())
    {
        paragraph.runs.push_back(TextLayoutRun{"", base_style_});
    }
    return {std::move(paragraph)};
}

std::optional<Rect> RichEditBoxControl::caret_bounds() const
{
    if (!is_focused() || !editable())
    {
        return std::nullopt;
    }
    const Rect area = text_area();
    sync_layout();
    if (layout_.lines().empty())
    {
        return Rect{area.x, area.y, 1.0f, area.height};
    }
    const Rect caret = layout_.caret_rect(caret_);
    return Rect{area.x + caret.x, area.y + caret.top() - scroll_y_, 1.0f, caret.height};
}

Rect RichEditBoxControl::text_area() const
{
    return bounds().inset(padding_);
}

Rect RichEditBoxControl::bar_viewport() const
{
    const Rect  area  = text_area();
    const float right = bounds().right() - 1.0f; // inside the border
    return Rect{area.x, area.y, std::max(0.0f, right - area.x), area.height};
}

void RichEditBoxControl::sync_layout() const
{
    if (!measurer_)
    {
        return;
    }
    TextLayoutOptions options;
    options.max_width  = wrapping_ != TextWrapping::NoWrap
                             ? (bounds().width > 0.0f ? text_area().width : layout_options_.max_width)
                             : 0.0f;
    options.wrapping   = wrapping_;
    options.alignment  = alignment_;
    options.base_style = base_style_;
    const bool changed = layout_.set_content(layout_content());
    if (changed || layout_.lines().empty() || options.max_width != layout_options_.max_width ||
        options.wrapping != layout_options_.wrapping || options.alignment != layout_options_.alignment ||
        !(options.base_style == layout_options_.base_style))
    {
        layout_options_ = options;
        layout_.layout(options, *measurer_);
    }
}

std::size_t RichEditBoxControl::hit_at(Point window_point) const
{
    sync_layout();
    const Rect area = text_area();
    return layout_.hit_test(Point{window_point.x - area.x, window_point.y - area.y + scroll_y_});
}

Size RichEditBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header      = header_.measure(available.width, context);
    available              = ControlHeader::below(available, header);
    const Size description = description_.measure(available.width, context);
    available              = ControlHeader::below(available, description);

    const Thickness padding = text_padding(context.theme);
    measurer_               = &context.measurer;
    bar_.apply_theme(context.theme);
    base_style_ = base_style(context.theme);
    padding_    = padding;

    // As TextBox: as wide as the text or the placeholder, as tall
    // as its lines, capped at what is offered.
    layout_.set_content(layout_content());
    layout_options_.wrapping   = wrapping_;
    layout_options_.alignment  = alignment_;
    layout_options_.base_style = base_style_;
    layout_options_.max_width  = wrapping_ != TextWrapping::NoWrap && std::isfinite(available.width)
                                     ? std::max(0.0f, available.width - padding.horizontal())
                                     : 0.0f;
    layout_.layout(layout_options_, context.measurer);

    const Size line = context.measurer.measure("Ag", base_style_);
    const float hint = placeholder_text_.empty() ? 0.0f : context.measurer.measure(placeholder_text_, base_style_).width;
    const float content = std::max(line.height, layout_.size().height);
    float       width   = std::max(layout_.size().width, hint) + padding.horizontal() + 1.0f; // and the caret
    width               = std::min(width, available.width);
    Size desired{width, content + padding.vertical()};
    desired.height = std::min(desired.height, available.height);
    Size total     = ControlHeader::above(desired, header);
    total.height   = std::max(total.height, context.theme.sizing.text_control_min_height.value_or(FieldMinHeight));
    return ControlHeader::above(total, description);
}

void RichEditBoxControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    description_.arrange(*this);
    sync_layout();
}

// ---- input -------------------------------------------------------------------------------

bool RichEditBoxControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    // The scroll bar takes its hover, drag and clicks before the text does.
    if (e.action != PointerAction::Wheel && !pointer_down_)
    {
        sync_layout();
        float offset = scroll_y_;
        if (bar_.handle_pointer(e, bar_viewport(), layout_.size().height, offset))
        {
            scroll_y_ = std::clamp(offset, 0.0f, std::max(0.0f, layout_.size().height - text_area().height));
            mark_dirty(DirtyFlags::Visual);
            return true;
        }
    }
    const auto before  = selection();
    bool       handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = bounds().contains(e.position);
            if (pointer_down_)
            {
                caret_ = hit_at(e.position);
                preferred_x_.reset();
            }
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && bounds().contains(e.position))
            {
                reset_caret_blink();
                pointer_down_         = true;
                hovered_              = true;
                handled               = true;
                const std::size_t hit = hit_at(e.position);
                if (e.click_count >= 3)
                {
                    std::size_t start = std::min(hit, text_.size());
                    std::size_t end   = start;
                    while (start > 0 && text_[start - 1] != '\n')
                    {
                        --start;
                    }
                    while (end < text_.size() && text_[end] != '\n')
                    {
                        ++end;
                    }
                    move_caret(start, false);
                    move_caret(end, true);
                }
                else if (e.click_count == 2)
                {
                    const auto [start, end] = word_range(hit);
                    move_caret(start, false);
                    move_caret(end, true);
                }
                else
                {
                    move_caret(hit, e.modifiers.shift);
                }
                preferred_x_.reset();
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
    mark_dirty(DirtyFlags::Visual);
    notify_selection(before);
    return handled;
}

bool RichEditBoxControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled() || e.horizontal_wheel || !bounds().contains(e.position))
    {
        return false;
    }
    sync_layout();
    const float max = std::max(0.0f, layout_.size().height - text_area().height);
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

bool RichEditBoxControl::takes_shortcut(const KeyEvent& e) const
{
    switch (e.key)
    {
        case Key::Delete:
        case Key::Backspace:
        case Key::Home:
        case Key::End:
        case Key::ArrowLeft:
        case Key::ArrowRight:
        case Key::ArrowUp:
        case Key::ArrowDown:
        case Key::PageUp:
        case Key::PageDown:
            return true;
        case Key::A:
        case Key::C:
        case Key::X:
        case Key::V:
        case Key::Y:
            return e.modifiers.ctrl && !e.modifiers.shift;
        case Key::Z:
            return e.modifiers.ctrl;
        case Key::B:
            return e.modifiers.ctrl && !e.modifiers.shift &&
                   !has_flag(disabled_shortcuts_, DisabledFormattingShortcuts::Bold);
        case Key::I:
            return e.modifiers.ctrl && !e.modifiers.shift &&
                   !has_flag(disabled_shortcuts_, DisabledFormattingShortcuts::Italic);
        case Key::U:
            return e.modifiers.ctrl && !e.modifiers.shift &&
                   !has_flag(disabled_shortcuts_, DisabledFormattingShortcuts::Underline);
        default:
            return false;
    }
}

bool RichEditBoxControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }
    const auto before = selection();
    reset_caret_blink();
    if (e.key != Key::ArrowUp && e.key != Key::ArrowDown && e.key != Key::PageUp && e.key != Key::PageDown)
    {
        preferred_x_.reset();
    }
    mark_dirty(DirtyFlags::Visual);

    // A user formatting command: toggles the selection (one undo step, one text change) or, at a
    // bare caret, what is typed next.
    const auto format_command = [&](std::optional<bool> CharacterFormat::* field, DisabledFormattingShortcuts flag)
    {
        if (!e.modifiers.ctrl || e.modifiers.shift || !editable() || has_flag(disabled_shortcuts_, flag))
        {
            return;
        }
        const bool selected = has_selection();
        toggle(field);
        if (selected)
        {
            user_changed();
        }
    };

    switch (e.key)
    {
        case Key::Enter:
            if (accepts_return_ && editable())
            {
                const std::string admitted = admit("\n");
                if (!admitted.empty())
                {
                    const auto [start, end]     = selection();
                    const RichTextFormat format = has_selection() ? format_at(start) : typing_format();
                    begin_edit(EditKind::Typing);
                    replace(start, end, admitted, format);
                    caret_  = start + 1;
                    anchor_ = caret_;
                    pending_format_.reset();
                    user_changed();
                }
            }
            break;

        case Key::ArrowLeft:
            if (!e.modifiers.shift && has_selection() && !e.modifiers.ctrl)
            {
                move_caret(selection().first, false); // collapse to the near boundary
            }
            else if (e.modifiers.ctrl)
            {
                move_caret(word_left(caret_), e.modifiers.shift);
            }
            else if (caret_ > 0)
            {
                move_caret(text_analyzer().previous_cluster(text_, caret_), e.modifiers.shift);
            }
            else
            {
                move_caret(0, e.modifiers.shift);
            }
            break;

        case Key::ArrowRight:
            if (!e.modifiers.shift && has_selection() && !e.modifiers.ctrl)
            {
                move_caret(selection().second, false);
            }
            else if (e.modifiers.ctrl)
            {
                move_caret(word_right(caret_), e.modifiers.shift);
            }
            else if (caret_ < text_.size())
            {
                move_caret(text_analyzer().next_cluster(text_, caret_), e.modifiers.shift);
            }
            else
            {
                move_caret(caret_, e.modifiers.shift);
            }
            break;

        case Key::ArrowUp:
        case Key::ArrowDown:
            move_vertically(e.key == Key::ArrowUp ? -1 : 1, e.modifiers.shift);
            break;

        case Key::PageUp:
        case Key::PageDown:
        {
            sync_layout();
            const float line_height = layout_.lines().empty() ? 1.0f : std::max(1.0f, layout_.lines().front().height);
            const int   page        = std::max(1, static_cast<int>(text_area().height / line_height) - 1);
            move_vertically(e.key == Key::PageUp ? -page : page, e.modifiers.shift);
            break;
        }

        case Key::Home:
            if (e.modifiers.ctrl)
            {
                move_caret(0, e.modifiers.shift);
            }
            else
            {
                sync_layout();
                move_caret(layout_.lines().empty() ? 0 : layout_.lines()[layout_.line_of(caret_)].begin, e.modifiers.shift);
            }
            break;

        case Key::End:
            if (e.modifiers.ctrl)
            {
                move_caret(text_.size(), e.modifiers.shift);
            }
            else
            {
                sync_layout();
                move_caret(layout_.lines().empty() ? text_.size() : visible_line_end(layout_, layout_.line_of(caret_)),
                           e.modifiers.shift);
            }
            break;

        case Key::Backspace:
            if (editable())
            {
                delete_backward(e.modifiers.ctrl);
            }
            break;

        case Key::Delete:
            if (editable())
            {
                delete_forward(e.modifiers.ctrl);
            }
            break;

        case Key::A:
            if (e.modifiers.ctrl)
            {
                move_caret(0, false);
                move_caret(text_.size(), true);
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
        case Key::Y:
            if (e.modifiers.ctrl && editable())
            {
                const bool redoing = e.key == Key::Y || e.modifiers.shift;
                if (redoing ? can_redo() : can_undo())
                {
                    redoing ? redo() : undo();
                    user_changed();
                }
            }
            break;

        case Key::B:
            format_command(&CharacterFormat::bold, DisabledFormattingShortcuts::Bold);
            break;
        case Key::I:
            format_command(&CharacterFormat::italic, DisabledFormattingShortcuts::Italic);
            break;
        case Key::U:
            format_command(&CharacterFormat::underline, DisabledFormattingShortcuts::Underline);
            break;

        default:
            break;
    }
    notify_selection(before);
    return handled;
}

bool RichEditBoxControl::text_override(const TextInputEvent& text_event)
{
    const std::string_view text = text_event.text;
    assert(is_valid_utf8(text));
    // Control characters, a tab included, come through handle_key().
    std::string typed = strip_controls(text, false);
    std::erase(typed, '\t');
    if (!editable() || !is_focused() || typed.empty())
    {
        return true;
    }
    reset_caret_blink();
    preferred_x_.reset();
    const std::string admitted = admit(typed);
    if (admitted.empty())
    {
        return true; // max_length reached: the keystroke is dropped, as RichEdit drops it
    }
    const auto before           = selection();
    const auto [start, end]     = before;
    const RichTextFormat format = has_selection() ? format_at(start) : typing_format();
    begin_edit(has_selection() ? EditKind::Other : EditKind::Typing);
    last_edit_ = EditKind::Typing;
    replace(start, end, admitted, format);
    caret_  = start + admitted.size();
    anchor_ = caret_;
    pending_format_.reset();
    user_changed();
    notify_selection(before);
    return true;
}

// ---- context menu ----------------------------------------------------------------------

bool RichEditBoxControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    if (!is_enabled())
    {
        return false;
    }
    reset_caret_blink();
    last_edit_ = EditKind::Other;
    if (spell_check_pending_)
    {
        run_spell_check();
    }
    const auto before = selection();

    sync_layout();
    std::size_t at = caret_;
    Point       anchor{};
    if (position)
    {
        // Right-click moves the caret, except inside the selection, so Cut/Copy still act on it.
        at                      = hit_at(*position);
        const auto [start, end] = selection();
        if (!(has_selection() && at >= start && at <= end))
        {
            move_caret(at, false);
        }
        anchor = *position;
    }
    else
    {
        const Rect area  = text_area();
        const Rect caret = layout_.caret_rect(caret_);
        anchor           = Point{area.x + caret.x, area.y + caret.bottom() - scroll_y_};
    }
    mark_dirty(DirtyFlags::Visual);
    notify_selection(before);

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
        Bold,
        Italic,
        Underline,
        Undo,
        Redo,
        SelectAll,
        Extra,
    };
    std::vector<ContextMenuItem>                 items;
    std::vector<std::pair<Command, std::size_t>> commands; // parallel to items
    std::vector<std::string>                     suggestions;
    const auto add = [&](std::string text, bool enabled, Command command, std::size_t payload = 0)
    {
        items.push_back(ContextMenuItem{std::move(text), enabled, false});
        commands.emplace_back(command, payload);
    };
    const auto separator = [&]
    {
        if (!items.empty() && !items.back().is_separator)
        {
            items.push_back(ContextMenuItem::separator());
            commands.emplace_back(Command::None, 0);
        }
    };

    // The spelling section, as TextBox's.
    std::optional<SpellingError> error;
    if (spell_checker_ && spelling_on() && editable())
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

    // Each command only where it applies.
    const bool can_copy = has_selection();
    if (editable() && can_copy)
    {
        add(t.commands.cut.text, true, Command::Cut);
    }
    if (can_copy)
    {
        add(t.commands.copy.text, true, Command::Copy);
    }
    if (editable() && clipboard().text().has_value())
    {
        add(t.commands.paste.text, true, Command::Paste);
    }
    if (editable())
    {
        if (!has_flag(disabled_shortcuts_, DisabledFormattingShortcuts::Bold))
        {
            add(t.text_menu.bold, true, Command::Bold);
        }
        if (!has_flag(disabled_shortcuts_, DisabledFormattingShortcuts::Italic))
        {
            add(t.text_menu.italic, true, Command::Italic);
        }
        if (!has_flag(disabled_shortcuts_, DisabledFormattingShortcuts::Underline))
        {
            add(t.text_menu.underline, true, Command::Underline);
        }
    }
    separator();
    if (editable() && can_undo())
    {
        add(t.commands.undo.text, true, Command::Undo);
    }
    if (editable() && can_redo())
    {
        add(t.commands.redo.text, true, Command::Redo);
    }
    if (!text_.empty())
    {
        add(t.commands.select_all.text, true, Command::SelectAll);
    }
    if (!extra_menu_items_.empty())
    {
        separator();
        for (std::size_t i = 0; i < extra_menu_items_.size(); ++i)
        {
            add(extra_menu_items_[i].text, true, Command::Extra, i);
        }
    }
    if (!items.empty() && items.back().is_separator)
    {
        items.pop_back();
        commands.pop_back();
    }
    if (items.empty())
    {
        return true;
    }

    const std::optional<std::size_t> choice = host.show(anchor, items);
    if (!choice || *choice >= commands.size())
    {
        return true;
    }
    const auto chosen_before      = selection();
    const auto [command, payload] = commands[*choice];
    switch (command)
    {
        case Command::Suggestion:
        {
            const std::string& replacement = suggestions[payload];
            begin_edit(EditKind::Other);
            replace(error->start, error->start + error->length, replacement, format_at(error->start));
            move_caret(error->start + replacement.size(), false);
            user_changed();
            break;
        }
        case Command::DeleteWord:
        {
            // Takes the whitespace in front of the repeated word with it: "the the" -> "the".
            std::size_t start = error->start;
            while (start > 0 && is_space(code_point_before(text_, start)) && text_[start - 1] != '\n')
            {
                start = previous_code_point(text_, start);
            }
            begin_edit(EditKind::Other);
            replace(start, error->start + error->length, "", {});
            move_caret(start, false);
            user_changed();
            break;
        }
        case Command::AddToDictionary:
            spell_checker_->add_to_dictionary(word);
            run_spell_check();
            break;
        case Command::Ignore:
            spell_checker_->ignore_for_session(word);
            run_spell_check();
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
        case Command::Bold:
        case Command::Italic:
        case Command::Underline:
        {
            const bool selected = has_selection();
            toggle(command == Command::Bold     ? &CharacterFormat::bold
                   : command == Command::Italic ? &CharacterFormat::italic
                                                : &CharacterFormat::underline);
            if (selected)
            {
                user_changed();
            }
            break;
        }
        case Command::Undo:
            undo();
            user_changed();
            break;
        case Command::Redo:
            redo();
            user_changed();
            break;
        case Command::SelectAll:
            move_caret(0, false);
            move_caret(text_.size(), true);
            break;
        case Command::Extra:
            if (extra_menu_items_[payload].on_click)
            {
                extra_menu_items_[payload].on_click();
            }
            break;
        case Command::None:
            break;
    }
    mark_dirty(DirtyFlags::Visual);
    notify_selection(chosen_before);
    return true;
}

// ---- time --------------------------------------------------------------------------------

void RichEditBoxControl::tick_override(Duration delta)
{
    if (spell_checker_ && spelling_on())
    {
        if (!spell_check_pending_ && spell_checker_->generation() != spell_generation_)
        {
            spell_check_pending_  = true;
            spell_check_delay_ms_ = 0.0;
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

    if (bar_.update(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (const std::optional<Duration> wait = bar_.fade_wait())
    {
        request_tick_in(*wait);
    }

    if (!is_focused() || !editable())
    {
        return;
    }
    caret_blink_ms_ += delta.count();
    if (caret_blink_ms_ >= CaretBlinkIntervalMs)
    {
        caret_blink_ms_ = std::fmod(caret_blink_ms_, CaretBlinkIntervalMs); // a long gap toggles once
        caret_visible_  = !caret_visible_;
        mark_dirty(DirtyFlags::Visual);
    }
    // The next blink is a timed step: the window sleeps until then.
    request_tick_in(Duration{CaretBlinkIntervalMs - caret_blink_ms_});
}

// ---- render --------------------------------------------------------------------------------

void RichEditBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    description_.render(renderer, theme, is_enabled());
    detail::draw_text_control_chrome(renderer, theme, bounds(), is_enabled(), is_focused(), hovered_, description_.has_error());

    const Rect  text_rect  = text_area();
    const Color text_color = is_enabled() ? foreground_.resolve(theme).value_or(theme.colors().field_text)
                                          : theme.button_disabled.text;
    const Color hint       = is_enabled() ? theme.colors().field_text.with_alpha(0.6f) : theme.button_disabled.text;

    sync_layout();
    // Bring the caret into view when it moved, so the wheel and the scroll bar can still look
    // elsewhere while the box has focus. Never scroll past the text.
    if (is_focused() && !layout_.lines().empty() && caret_ != revealed_caret_)
    {
        revealed_caret_  = caret_;
        const Rect caret = layout_.caret_rect(caret_);
        if (caret.bottom() > scroll_y_ + text_rect.height)
        {
            scroll_y_ = caret.bottom() - text_rect.height;
        }
        if (caret.top() < scroll_y_)
        {
            scroll_y_ = caret.top();
        }
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f, std::max(0.0f, layout_.size().height - text_rect.height));

    renderer.push_clip(text_rect);
    const Point origin{text_rect.x, text_rect.y - scroll_y_};
    if (text_.empty() && !placeholder_text_.empty())
    {
        TextStyle hint_style = base_style(theme);
        hint_style.vertical  = TextAlign::Leading;
        hint_style.wrapping  = wrapping_;
        renderer.draw_text(placeholder_text_, text_rect, hint, hint_style);
    }
    else
    {
        // Character backgrounds are highlights under the text; the selection goes over them.
        std::vector<TextHighlight> backgrounds;
        std::size_t                pos = 0;
        for (const Run& run : runs_)
        {
            if (run.format.background)
            {
                backgrounds.push_back(TextHighlight{pos, pos + run.length, run.format.background, std::nullopt});
            }
            pos += run.length;
        }
        TextLayout::Paint paint;
        paint.foreground = text_color;
        paint.highlights = backgrounds;
        if (has_selection() && is_focused())
        {
            const auto [start, end] = selection();
            paint.selection         = TextHighlight{start,
                                                    end,
                                                    selection_color_.resolve(theme).value_or(theme.colors().selection),
                                                    theme.colors().selection_text};
        }
        layout_.render(renderer, origin, paint);
    }
    if (is_enabled())
    {
        for (const SpellingError& error : spelling_errors_)
        {
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
    // Draws nothing while the text fits.
    bar_.render(renderer, theme, bar_viewport(), layout_.size().height, scroll_y_);
}

std::unique_ptr<ui::Element> RichEditBox::build() &&
{
    auto                    node = std::make_unique<ui::RichEditBoxControl>();
    ui::RichEditBoxControl* raw  = node.get();
    apply(header, *node, [raw](const std::string& value) { raw->set_header(value); });
    apply(description, *node, [raw](const std::string& value) { raw->set_description(value); });
    apply(error, *node, [raw](const std::string& value) { raw->set_error(value); });
    // A text the box already holds changes nothing, so the user's formatting survives the value
    // coming back from the property it was just written to.
    apply(text,
          *node,
          [raw](const std::string& value)
          {
              if (value != raw->text())
              {
                  raw->set_text(value);
              }
          });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(spell_check, *node, [raw = node.get()](const bool& v) { raw->set_spell_check(v); });
    apply(placeholder_text, *node, [raw = node.get()](const std::string& value) { raw->set_placeholder_text(value); });
    apply(read_only, *node, [raw = node.get()](const bool& value) { raw->set_read_only(value); });
    apply(accepts_return, *node, [raw = node.get()](const bool& v) { raw->set_accepts_return(v); });
    apply(wrapping, *node, [raw = node.get()](const TextWrapping& v) { raw->set_wrapping(v); });
    apply(text_alignment, *node, [raw = node.get()](const TextAlignment& v) { raw->set_text_alignment(v); });
    if (font_family.reactive() || font_family.initial())
    {
        apply(font_family, *node, [raw = node.get()](const std::optional<std::string>& v) { raw->set_font_family(v); });
    }
    if (font_size.reactive() || font_size.initial())
    {
        apply(font_size, *node, [raw = node.get()](const std::optional<float>& v) { raw->set_font_size(v); });
    }
    apply(foreground, *node, [raw = node.get()](const Paint& v) { raw->set_foreground(v); });
    apply(selection_highlight_color,
          *node,
          [raw = node.get()](const Paint& v)
          { ui::detail::set_paint(v, [raw](auto c) { raw->set_selection_highlight_color(c); }); });
    apply(max_length, *node, [raw = node.get()](const std::size_t& value) { raw->set_max_length(value); });
    apply(character_casing,
          *node,
          [raw = node.get()](const ui::CharacterCasing& value) { raw->set_character_casing(value); });
    apply(disabled_formatting_shortcuts,
          *node,
          [raw = node.get()](const DisabledFormattingShortcuts& v) { raw->set_disabled_formatting_shortcuts(v); });

    // Two-way: what the user types is written back as plain text, then the spec's handler runs.
    if (auto changed = ui::write_back(text, std::move(on_text_changed)))
    {
        node->on_text_changed(std::move(changed));
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

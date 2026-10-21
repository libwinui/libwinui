// SPDX-License-Identifier: MIT
#include "winui/render/TextLayout.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"

#include <algorithm>
#include <optional>
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
constexpr float FitEpsilon = 0.01f;
// Enough for every word of a long document; past it the cache starts over.
constexpr std::size_t MaxCachedMeasurements = 200000;

bool is_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

// Two contents that differ only in color and tag measure the same.
bool same_shape(const std::vector<TextLayoutParagraph>& a, const std::vector<TextLayoutParagraph>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t p = 0; p < a.size(); ++p)
    {
        if (a[p].margin != b[p].margin || a[p].runs.size() != b[p].runs.size())
        {
            return false;
        }
        for (std::size_t r = 0; r < a[p].runs.size(); ++r)
        {
            if (a[p].runs[r].text != b[p].runs[r].text || a[p].runs[r].style != b[p].runs[r].style)
            {
                return false;
            }
        }
    }
    return true;
}

// The code-point boundaries in [begin, end) of `text` after `begin` itself, `end` included:
// where a word may be split or a caret land.
std::vector<std::size_t> boundaries(std::string_view text, std::size_t begin, std::size_t end)
{
    std::vector<std::size_t> out;
    for (std::size_t i = next_code_point(text, begin); i < end; i = next_code_point(text, i))
    {
        out.push_back(i);
    }
    if (begin < end)
    {
        out.push_back(end);
    }
    return out;
}

TextStyle draw_style(TextStyle style)
{
    style.horizontal = TextAlign::Leading;
    style.vertical   = TextAlign::Center;
    style.wrapping   = TextWrapping::NoWrap;
    return style;
}
} // namespace

bool TextLayout::set_content(std::vector<TextLayoutParagraph> paragraphs)
{
    if (paragraphs == paragraphs_)
    {
        return false;
    }
    const bool shape_changed = !same_shape(paragraphs, paragraphs_);
    paragraphs_              = std::move(paragraphs);
    rebuild_index();
    if (shape_changed)
    {
        lines_.clear();
        fragments_.clear();
        size_ = {};
    }
    return shape_changed;
}

// The index points into the paragraphs, so a copy rebuilds its own over the copied ones.
TextLayout::TextLayout(const TextLayout& other) :
    paragraphs_(other.paragraphs_),
    lines_(other.lines_),
    fragments_(other.fragments_),
    size_(other.size_),
    start_(other.start_),
    cache_(other.cache_),
    cache_entries_(other.cache_entries_),
    measurer_(other.measurer_)
{
    rebuild_index();
}

TextLayout& TextLayout::operator=(const TextLayout& other)
{
    if (this != &other)
    {
        TextLayout copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void TextLayout::rebuild_index()
{
    cached_run_ = detail::NoIndex;
    runs_.clear();
    paragraph_begin_.clear();
    text_.clear();
    for (std::size_t p = 0; p < paragraphs_.size(); ++p)
    {
        if (p > 0)
        {
            text_ += '\n';
        }
        paragraph_begin_.push_back(text_.size());
        for (const TextLayoutRun& run : paragraphs_[p].runs)
        {
            runs_.push_back(RunRef{&run, text_.size(), p});
            text_ += run.text;
        }
    }
}

Size TextLayout::measure_range(std::size_t run, std::size_t begin, std::size_t end) const
{
    if (run != cached_run_)
    {
        const TextStyle style = draw_style(runs_[run].run->style);
        auto bucket = std::ranges::find_if(cache_, [&style](const StyleCache& c) { return c.style == style; });
        if (bucket == cache_.end())
        {
            cache_.push_back(StyleCache{.style = style, .sizes = {}});
            bucket = cache_.end() - 1;
        }
        cached_run_    = run;
        cached_bucket_ = static_cast<std::size_t>(bucket - cache_.begin());
    }
    StyleCache&            bucket = cache_[cached_bucket_];
    const TextStyle&       style  = bucket.style;
    const std::string_view piece  = std::string_view{text_}.substr(begin, end - begin);
    if (const auto it = bucket.sizes.find(piece); it != bucket.sizes.end())
    {
        return it->second;
    }
    Size size{};
    if (measurer_)
    {
        size = measurer_->measure(piece, style);
        if (begin == end || size.height <= 0.0f)
        {
            // An empty range still has a line's height (an empty line, a line break).
            size.height = measurer_->measure(" ", style).height;
        }
    }
    if (cache_entries_ >= MaxCachedMeasurements)
    {
        for (StyleCache& c : cache_)
        {
            c.sizes.clear();
        }
        cache_entries_ = 0;
    }
    bucket.sizes.emplace(std::string{piece}, size);
    ++cache_entries_;
    return size;
}

void TextLayout::layout(const TextLayoutOptions& options, const TextMeasurer& measurer, std::size_t start)
{
    if (measurer_ != &measurer)
    {
        cache_.clear();
        cache_entries_ = 0;
        cached_run_    = detail::NoIndex;
        measurer_      = &measurer;
    }
    lines_.clear();
    fragments_.clear();
    size_  = {};
    start_ = start;

    const bool  bounded     = options.max_width > 0.0f && std::isfinite(options.max_width);
    const bool  wrapping    = bounded && options.wrapping != TextWrapping::NoWrap;
    const float base_height = std::max(1.0f, measurer.measure(" ", draw_style(options.base_style)).height);

    // Pass 1: break every paragraph into lines of pieces.
    std::vector<std::vector<Piece>> line_pieces;
    float                           y      = 0.0f;
    float                           widest = 0.0f;

    for (std::size_t p = 0; p < paragraphs_.size(); ++p)
    {
        const std::size_t pbegin = paragraph_begin_[p];
        std::size_t       pend   = pbegin;
        for (const TextLayoutRun& run : paragraphs_[p].runs)
        {
            pend += run.text.size();
        }
        if (pend < start)
        {
            continue;
        }
        const bool       continuing = start > pbegin;
        const Thickness& margin     = paragraphs_[p].margin;
        if (!continuing)
        {
            y += margin.top;
        }
        const float avail = bounded ? std::max(0.0f, options.max_width - margin.left - margin.right) : 0.0f;

        // Tokenize into pieces: a word and the spaces after it, cut at run ends; `\n` alone.
        std::vector<Piece> pieces;
        float              empty_height = 0.0f;
        for (std::size_t r = 0; r < runs_.size(); ++r)
        {
            if (runs_[r].paragraph != p)
            {
                continue;
            }
            const std::size_t rbegin = runs_[r].begin;
            const std::size_t rend   = rbegin + runs_[r].run->text.size();
            if (empty_height <= 0.0f)
            {
                empty_height = measure_range(r, rbegin, rbegin).height;
            }
            std::size_t i = std::max(rbegin, start);
            while (i < rend)
            {
                Piece piece;
                piece.run   = r;
                piece.begin = i;
                if (text_[i] == '\n')
                {
                    piece.end = piece.trim_end = i + 1;
                    piece.hard_break           = true;
                    piece.height               = measure_range(r, i, i).height;
                    pieces.push_back(piece);
                    ++i;
                    continue;
                }
                while (i < rend && text_[i] != '\n' && !is_space(text_[i]))
                {
                    ++i;
                }
                piece.trim_end = i;
                while (i < rend && is_space(text_[i]))
                {
                    ++i;
                }
                piece.end         = i;
                piece.break_after = piece.end > piece.trim_end;
                const Size full   = measure_range(r, piece.begin, piece.end);
                piece.width       = full.width;
                piece.height      = full.height;
                piece.trim_width = piece.trim_end == piece.end ? full.width
                                   : piece.trim_end == piece.begin ? 0.0f
                                                                   : measure_range(r, piece.begin, piece.trim_end).width;
                pieces.push_back(piece);
            }
        }
        if (empty_height <= 0.0f)
        {
            empty_height = base_height;
        }

        std::vector<Piece> current;
        float              x      = 0.0f;
        std::size_t        cursor = std::max(pbegin, start); // where an empty line sits

        const auto finish = [&](bool hard, std::size_t end_offset, std::size_t next_offset)
        {
            Line line;
            line.paragraph  = p;
            line.hard_break = hard;
            line.begin      = current.empty() ? cursor : current.front().begin;
            line.end        = end_offset;
            line.next       = next_offset;
            line.top        = y;
            float height    = 0.0f;
            float width     = 0.0f;
            for (std::size_t k = 0; k < current.size(); ++k)
            {
                height = std::max(height, current[k].height);
                width += k + 1 == current.size() ? current[k].trim_width : current[k].width;
            }
            line.height = height > 0.0f ? height : empty_height;
            line.width  = width;
            y += line.height;
            widest = std::max(widest, margin.left + width + margin.right);
            lines_.push_back(line);
            line_pieces.push_back(std::move(current));
            current.clear();
            x      = 0.0f;
            cursor = next_offset;
        };
        const auto soft_break = [&]
        {
            const std::size_t end = current.back().end;
            finish(false, end, end);
        };
        const auto add = [&](const Piece& piece)
        {
            current.push_back(piece);
            x += piece.width;
        };

        std::size_t i = 0;
        while (i < pieces.size())
        {
            if (pieces[i].hard_break)
            {
                const std::size_t at = pieces[i].begin;
                finish(true, at, at + 1);
                ++i;
                continue;
            }
            std::size_t j = i;
            while (!pieces[j].break_after && j + 1 < pieces.size() && !pieces[j + 1].hard_break)
            {
                ++j;
            }
            float unit_trim = 0.0f;
            for (std::size_t k = i; k <= j; ++k)
            {
                unit_trim += k == j ? pieces[k].trim_width : pieces[k].width;
            }
            if (wrapping && !current.empty() && x + unit_trim > avail + FitEpsilon)
            {
                soft_break();
            }
            if (wrapping && options.wrapping == TextWrapping::Wrap && unit_trim > avail + FitEpsilon)
            {
                // A word longer than a whole line: split it between characters.
                for (std::size_t k = i; k <= j; ++k)
                {
                    Piece piece = pieces[k];
                    for (;;)
                    {
                        if (x + piece.trim_width <= avail + FitEpsilon)
                        {
                            add(piece);
                            break;
                        }
                        if (!current.empty())
                        {
                            soft_break();
                            continue;
                        }
                        // The most characters (at least one) that fit the line, cut between code
                        // points.
                        const std::vector<std::size_t> cuts = boundaries(text_, piece.begin, piece.trim_end);
                        std::size_t                    lo   = 0;
                        std::size_t                    hi   = cuts.size() - 1;
                        while (lo < hi)
                        {
                            const std::size_t mid = lo + (hi - lo + 1) / 2;
                            if (measure_range(piece.run, piece.begin, cuts[mid]).width <= avail + FitEpsilon)
                            {
                                lo = mid;
                            }
                            else
                            {
                                hi = mid - 1;
                            }
                        }
                        Piece head = piece;
                        head.end = head.trim_end = cuts[lo];
                        head.break_after         = false;
                        const Size hs            = measure_range(piece.run, head.begin, head.end);
                        head.width = head.trim_width = hs.width;
                        head.height                  = hs.height;
                        add(head);
                        soft_break();

                        piece.begin = head.end;
                        if (piece.begin >= piece.end)
                        {
                            break;
                        }
                        const Size rest  = measure_range(piece.run, piece.begin, piece.end);
                        piece.width      = rest.width;
                        piece.trim_width = piece.trim_end == piece.end ? rest.width
                                           : piece.trim_end == piece.begin
                                               ? 0.0f
                                               : measure_range(piece.run, piece.begin, piece.trim_end).width;
                        if (piece.trim_end <= piece.begin)
                        {
                            add(piece); // only its trailing spaces are left
                            break;
                        }
                    }
                }
            }
            else
            {
                for (std::size_t k = i; k <= j; ++k)
                {
                    add(pieces[k]);
                }
            }
            i = j + 1;
        }
        // The paragraph's end closes its last line: an empty one after a trailing line break, or for a
        // paragraph with no text at all.
        const bool last_paragraph = p + 1 == paragraphs_.size();
        finish(true, pend, last_paragraph ? pend : pend + 1);
        y += margin.bottom;
    }
    size_ = {widest, y};

    // Pass 2: place the lines and cut them into fragments.
    const float align_width = bounded ? options.max_width : widest;
    for (std::size_t l = 0; l < lines_.size(); ++l)
    {
        Line&               line   = lines_[l];
        std::vector<Piece>& pieces = line_pieces[l];
        const Thickness&    margin = paragraphs_[line.paragraph].margin;
        const float         slack  = std::max(0.0f, align_width - margin.left - margin.right - line.width);

        float left = margin.left;
        float gap  = 0.0f;
        switch (options.alignment)
        {
            case TextAlignment::Left:
                break;
            case TextAlignment::Center:
                left += slack * 0.5f;
                break;
            case TextAlignment::Right:
                left += slack;
                break;
            case TextAlignment::Justify:
            {
                std::size_t gaps = 0;
                for (std::size_t k = 0; k + 1 < pieces.size(); ++k)
                {
                    gaps += pieces[k].break_after ? 1 : 0;
                }
                if (bounded && !line.hard_break && gaps > 0)
                {
                    gap = slack / static_cast<float>(gaps);
                }
                break;
            }
        }
        line.left = left;

        float x = left;
        for (Piece& piece : pieces)
        {
            piece.x = x;
            x += piece.width + (piece.break_after ? gap : 0.0f);
        }

        line.fragment_begin = fragments_.size();
        for (std::size_t k = 0; k < pieces.size(); ++k)
        {
            const Piece& piece = pieces[k];
            const bool   merge = !fragments_.empty() && fragments_.size() > line.fragment_begin &&
                                 fragments_.back().run == piece.run && fragments_.back().end == piece.begin &&
                                 (gap == 0.0f || !pieces[k - 1].break_after);
            if (merge)
            {
                Fragment& f = fragments_.back();
                f.end       = piece.end;
                f.rect.width += piece.width;
                if (piece.height > f.rect.height)
                {
                    f.rect.height = piece.height;
                }
                continue;
            }
            Fragment f;
            f.run   = piece.run;
            f.begin = piece.begin;
            f.end   = piece.end;
            f.rect  = Rect{piece.x, 0.0f, piece.width, piece.height};
            fragments_.push_back(std::move(f));
        }
        line.fragment_end = fragments_.size();
        for (std::size_t f = line.fragment_begin; f < line.fragment_end; ++f)
        {
            Fragment& frag = fragments_[f];
            frag.rect.y    = line.top + line.height - frag.rect.height;
            frag.text      = text_.substr(frag.begin, frag.end - frag.begin);
        }
    }
}

std::size_t TextLayout::fitting_lines(float height) const
{
    std::size_t count = 0;
    while (count < lines_.size() && lines_[count].top + lines_[count].height <= height + FitEpsilon)
    {
        ++count;
    }
    return count;
}

float TextLayout::height_of(std::size_t count) const
{
    if (count == 0 || lines_.empty())
    {
        return 0.0f;
    }
    if (count >= lines_.size())
    {
        return size_.height;
    }
    const Line& last = lines_[count - 1];
    return last.top + last.height;
}

float TextLayout::x_in_line(const Line& line, std::size_t offset) const
{
    if (line.fragment_begin == line.fragment_end)
    {
        return line.left;
    }
    const Fragment& first = fragments_[line.fragment_begin];
    if (offset <= first.begin)
    {
        return first.rect.x;
    }
    for (std::size_t f = line.fragment_begin; f < line.fragment_end; ++f)
    {
        const Fragment& frag = fragments_[f];
        if (offset >= frag.begin && offset <= frag.end)
        {
            if (offset == frag.begin)
            {
                return frag.rect.x;
            }
            return frag.rect.x + measure_range(frag.run, frag.begin, offset).width;
        }
    }
    const Fragment& last = fragments_[line.fragment_end - 1];
    return last.rect.x + last.rect.width;
}

std::size_t TextLayout::line_of(std::size_t offset) const
{
    std::size_t result = 0;
    for (std::size_t l = 0; l < lines_.size(); ++l)
    {
        if (lines_[l].begin <= offset)
        {
            result = l;
        }
        else
        {
            break;
        }
    }
    return result;
}

std::size_t TextLayout::offset_at_x(std::size_t index, float x) const
{
    if (lines_.empty())
    {
        return start_;
    }
    const Line& line = lines_[std::min(index, lines_.size() - 1)];

    // Past the end: the line's end, before a wrapped line's hanging space, so the caret stays
    // on this line rather than jumping to the start of the next.
    std::size_t line_end = line.end;
    if (!line.hard_break && line_end > line.begin && is_space(text_[line_end - 1]))
    {
        --line_end;
    }
    if (line.fragment_begin == line.fragment_end)
    {
        return line.begin;
    }
    if (x <= fragments_[line.fragment_begin].rect.x)
    {
        return line.begin;
    }
    for (std::size_t f = line.fragment_begin; f < line.fragment_end; ++f)
    {
        const Fragment& frag = fragments_[f];
        if (x >= frag.rect.x + frag.rect.width)
        {
            continue;
        }
        if (x <= frag.rect.x)
        {
            return frag.begin; // in a justified gap: the start of the word after it
        }
        // The code point under x, then its nearer edge.
        const float                    local = x - frag.rect.x;
        const std::vector<std::size_t> cuts  = boundaries(text_, frag.begin, frag.end);
        std::size_t                    lo    = 0;
        std::size_t                    hi    = cuts.size();
        while (lo < hi)
        {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (measure_range(frag.run, frag.begin, cuts[mid]).width <= local)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        std::size_t at = lo == 0 ? frag.begin : cuts[lo - 1];
        if (lo < cuts.size())
        {
            const float before = lo == 0 ? 0.0f : measure_range(frag.run, frag.begin, cuts[lo - 1]).width;
            const float after  = measure_range(frag.run, frag.begin, cuts[lo]).width;
            if (local - before > after - local)
            {
                at = cuts[lo];
            }
        }
        return std::min(at, line_end);
    }
    return line_end;
}

std::size_t TextLayout::hit_test(Point p) const
{
    if (lines_.empty())
    {
        return start_;
    }
    std::size_t index = lines_.size() - 1;
    for (std::size_t l = 0; l < lines_.size(); ++l)
    {
        if (p.y < lines_[l].top + lines_[l].height)
        {
            index = l;
            break;
        }
    }
    return offset_at_x(index, p.x);
}

Rect TextLayout::caret_rect(std::size_t offset) const
{
    if (lines_.empty())
    {
        return {};
    }
    const Line& line = lines_[line_of(offset)];
    return Rect{x_in_line(line, std::min(offset, line.end)), line.top, 0.0f, line.height};
}

std::vector<Rect> TextLayout::range_rects(std::size_t begin, std::size_t end) const
{
    std::vector<Rect> out;
    if (begin >= end)
    {
        return out;
    }
    for (const Line& line : lines_)
    {
        const std::size_t b = std::max(begin, line.begin);
        const std::size_t e = std::min(end, line.end);
        if (b >= e)
        {
            continue;
        }
        const float x0 = x_in_line(line, b);
        const float x1 = x_in_line(line, e);
        if (x1 > x0)
        {
            out.push_back(Rect{x0, line.top, x1 - x0, line.height});
        }
    }
    return out;
}

std::optional<std::size_t> TextLayout::tag_at(Point p) const
{
    for (const Line& line : lines_)
    {
        if (p.y < line.top || p.y >= line.top + line.height)
        {
            continue;
        }
        for (std::size_t f = line.fragment_begin; f < line.fragment_end; ++f)
        {
            const Fragment& frag = fragments_[f];
            if (p.x >= frag.rect.x && p.x < frag.rect.x + frag.rect.width)
            {
                return runs_[frag.run].run->tag;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void TextLayout::render(Renderer& renderer, Point origin, const Paint& paint) const
{
    const std::size_t first = paint.first_line;
    const std::size_t last  = std::min(paint.last_line.value_or(lines_.size()), lines_.size());
    if (first >= last)
    {
        return;
    }
    const float dx = origin.x;
    const float dy = origin.y - lines_[first].top;

    const auto visible_rects = [&](std::size_t begin, std::size_t end)
    {
        std::vector<Rect> out;
        for (std::size_t l = first; l < last && begin < end; ++l)
        {
            const Line&       line = lines_[l];
            const std::size_t b    = std::max(begin, line.begin);
            const std::size_t e    = std::min(end, line.end);
            if (b >= e)
            {
                continue;
            }
            const float x0 = x_in_line(line, b);
            const float x1 = x_in_line(line, e);
            if (x1 > x0)
            {
                out.push_back(Rect{dx + x0, dy + line.top, x1 - x0, line.height});
            }
        }
        return out;
    };
    const auto fragment_color = [&](const Fragment& frag)
    {
        const TextLayoutRun& run = *runs_[frag.run].run;
        if (paint.tag_color && run.tag)
        {
            if (std::optional<Color> c = paint.tag_color(*run.tag))
            {
                return *c;
            }
        }
        return run.color.value_or(paint.foreground);
    };
    const auto draw_fragment = [&](const Fragment& frag, const Color& color)
    {
        const TextStyle style = draw_style(runs_[frag.run].run->style);
        // An italic's last glyph leans past its advance, and draw_text clips to its box.
        const float lean = style.italic ? style.font_size * 0.5f : 0.0f;
        const Rect  rect{dx + frag.rect.x, dy + frag.rect.y, frag.rect.width + lean, frag.rect.height};
        renderer.draw_text(frag.text, rect, color, style);
    };

    std::vector<TextHighlight> overlays(paint.highlights.begin(), paint.highlights.end());
    if (paint.selection)
    {
        overlays.push_back(*paint.selection);
    }
    for (const TextHighlight& h : overlays)
    {
        if (h.background)
        {
            for (const Rect& r : visible_rects(h.begin, h.end))
            {
                renderer.fill_rounded_rect(r, 0.0f, *h.background);
            }
        }
    }

    // A fragment some color spans touch is drawn in pieces: each stretch at its x in the line.
    const auto draw_colored = [&](const Fragment& frag, const Color& base)
    {
        const std::span<const TextColorSpan> spans = paint.colors;
        auto it = std::ranges::partition_point(spans, [&frag](const TextColorSpan& s) { return s.end <= frag.begin; });
        if (it == spans.end() || it->begin >= frag.end ||
            std::string_view{text_}.substr(frag.begin, frag.end - frag.begin) != frag.text)
        {
            draw_fragment(frag, base);
            return;
        }
        const TextStyle style = draw_style(runs_[frag.run].run->style);
        const float     lean  = style.italic ? style.font_size * 0.5f : 0.0f;
        const auto      piece = [&](std::size_t b, std::size_t e, const Color& color)
        {
            const std::string_view part = std::string_view{text_}.substr(b, e - b);
            if (b >= e || std::ranges::all_of(part, is_space))
            {
                return;
            }
            const float x0 = b == frag.begin ? 0.0f : measure_range(frag.run, frag.begin, b).width;
            const float x1 = measure_range(frag.run, frag.begin, e).width;
            const Rect  rect{dx + frag.rect.x + x0, dy + frag.rect.y, x1 - x0 + lean, frag.rect.height};
            renderer.draw_text(part, rect, color, style);
        };
        std::size_t at = frag.begin;
        for (; it != spans.end() && it->begin < frag.end; ++it)
        {
            const std::size_t b = std::max(it->begin, at);
            const std::size_t e = std::min(it->end, frag.end);
            piece(at, b, base);
            piece(b, e, it->color);
            at = std::max(at, e);
        }
        piece(at, frag.end, base);
    };

    const std::size_t fbegin = lines_[first].fragment_begin;
    const std::size_t fend   = lines_[last - 1].fragment_end;
    for (std::size_t f = fbegin; f < fend; ++f)
    {
        const Fragment& frag = fragments_[f];
        if (frag.begin < frag.end)
        {
            if (paint.colors.empty())
            {
                draw_fragment(frag, fragment_color(frag));
            }
            else
            {
                draw_colored(frag, fragment_color(frag));
            }
        }
    }

    for (const TextHighlight& h : overlays)
    {
        if (!h.foreground)
        {
            continue;
        }
        for (const Rect& r : visible_rects(h.begin, h.end))
        {
            renderer.push_clip(r);
            for (std::size_t f = fbegin; f < fend; ++f)
            {
                const Fragment& frag = fragments_[f];
                if (frag.end > h.begin && frag.begin < h.end && frag.rect.y + dy < r.bottom() &&
                    frag.rect.y + frag.rect.height + dy > r.top())
                {
                    draw_fragment(frag, *h.foreground);
                }
            }
            renderer.pop_clip();
        }
    }
}

} // namespace ui

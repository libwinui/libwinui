// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/render/Text.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cstddef>

namespace ui
{

class Renderer;

/// Where each line sits across the width the text is laid out in.
/// Justify stretches every line but a paragraph's last (and one ended by a line break) to the
/// full width by widening the gaps between its words.
enum class TextAlignment
{
    Left,    ///< Lines start at the left edge; the default.
    Center,  ///< Lines are centered.
    Right,   ///< Lines end at the right edge.
    Justify, ///< Every line but a paragraph's last is stretched to the full width.
};

/// One run of same-formatted text in a TextLayout paragraph, with the formatting of any
/// enclosing spans already folded in.
///
/// Example:
/// @code
/// ui::TextLayoutRun bold{"Important: ", ui::TextStyle{.weight = ui::FontWeight::Bold}};
/// ui::TextLayoutRun link{"read more", {}, ui::Color::from_argb(0xFF0066CC), 0}; // tag 0
/// ui::TextLayoutParagraph paragraph{{bold, link}};
/// @endcode
struct TextLayoutRun
{
    /// UTF-8 text of the run; a `\n` inside it is a hard line break.
    std::string text;
    /// Font, size, weight, italic, decorations and character spacing. Its alignment and wrapping
    /// are ignored (the layout's options decide those).
    TextStyle style{};
    /// Unset draws in the layout's default foreground.
    std::optional<Color> color{};
    /// The caller's own id for the run, carried to hit testing (tag_at()), such as a hyperlink
    /// index; unset (the default) is none.
    std::optional<std::size_t> tag{};

    /// Memberwise equality.
    friend bool operator==(const TextLayoutRun&, const TextLayoutRun&) = default;
};

/// Runs that flow together, with a margin around the block. Paragraphs are
/// joined by one `\n` in TextLayout::text().
struct TextLayoutParagraph
{
    /// The runs, in reading order. An empty list still takes one line of
    /// TextLayoutOptions::base_style height.
    std::vector<TextLayoutRun> runs;
    /// Space around the paragraph in DIPs; vertical margins are not collapsed between paragraphs.
    Thickness margin{};

    /// Memberwise equality.
    friend bool operator==(const TextLayoutParagraph&, const TextLayoutParagraph&) = default;
};

/// How TextLayout::layout() breaks and aligns lines: the paragraph-level settings a TextControl
/// or RichText passes down.
struct TextLayoutOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The width lines break at, and the width alignment works in. 0 or less, or infinity, is
    /// unbounded: no line breaks but the hard ones, and lines aligned within the widest.
    float max_width = 0.0f;
    /// Whether and how lines break at `max_width`; NoWrap keeps only the hard breaks.
    TextWrapping wrapping = TextWrapping::NoWrap;
    /// Where each line sits across the width.
    TextAlignment alignment = TextAlignment::Left;
    /// The style an empty paragraph (or one with no runs) takes its line height from.
    TextStyle base_style{};
};

/// A range drawn with its own background and/or foreground, such as a highlight or the
/// selection.
struct TextHighlight
{
    /// First byte offset into TextLayout::text().
    std::size_t begin = 0;
    /// One past the last byte offset; an empty range draws nothing.
    std::size_t end = 0;
    /// Fill behind the range; unset leaves the background alone.
    std::optional<Color> background{};
    /// Text color inside the range; unset keeps the runs' own colors.
    std::optional<Color> foreground{};
};

/// A range of text drawn in its own color, such as a token a syntax highlighter colored. Unlike
/// a TextHighlight it only recolors: the text is drawn once, in pieces.
struct TextColorSpan
{
    /// First byte offset into the text.
    std::size_t begin = 0;
    /// One past the last byte offset; an empty range colors nothing.
    std::size_t end = 0;
    /// The text color inside the range.
    Color color{};

    friend bool operator==(const TextColorSpan&, const TextColorSpan&) = default;
};

/// Paragraphs of differently styled runs broken into lines, with the geometry selection needs:
/// the engine behind TextControl inlines, RichText and a multi-line TextBox.
///
/// Built only on TextMeasurer::measure(), so it lays out the same with every measurer.
/// Lines break greedily after runs of spaces (a trailing space hangs), a word spanning runs stays
/// whole, and under `Wrap` an overlong word is split between characters. Each piece between break
/// opportunities is measured once and cached, so laying out again at another width measures nothing.
/// All offsets address text(): every run's text in order, paragraphs joined by one `\n`. Mixed
/// font sizes on one line share a bottom edge rather than a baseline.
///
/// Call order: set_content(), then layout(), then the queries and render().
///
/// Example:
/// @code
/// ui::TextStyle body{.font_size = 14.0f, .horizontal = ui::TextAlign::Leading};
/// ui::TextStyle bold = body;
/// bold.weight           = ui::FontWeight::Bold;
///
/// ui::TextLayout layout; // a member of the element that shows the text
/// layout.set_content({
///     {{{"Hello ", bold}, {"world, this wraps.", body}}},
///     {{{"Second paragraph", body}}, ui::Edges{.top = 8}},
/// });
///
/// // In measure_override(), with the measurer it is handed:
/// ui::TextLayoutOptions options;
/// options.max_width  = 120.0f;
/// options.wrapping   = ui::TextWrapping::Wrap;
/// options.base_style = body;
/// layout.layout(options, measurer);
///
/// // In pointer_override():
/// const std::size_t caret = layout.hit_test({30.0f, 5.0f}); // click -> offset
/// const ui::Rect    bar   = layout.caret_rect(caret);       // where to draw the caret
///
/// // In render_override(), with the renderer it is handed:
/// ui::TextLayout::Paint paint;
/// paint.foreground = ui::colors::Black;
/// paint.selection  = ui::TextHighlight{0, 5, ui::colors::White, std::nullopt};
/// layout.render(renderer, {10.0f, 20.0f}, paint);
/// @endcode
class TextLayout
{
public:
    /// One drawn piece of a run on one line. Rect is relative to the layout's origin.
    struct Fragment
    {
        /// Bounds of the piece in layout coordinates (DIPs).
        Rect        rect{};
        std::size_t run   = 0; ///< index into runs() (the paragraphs' runs, flattened)
        std::size_t begin = 0; ///< offsets into text()
        std::size_t end   = 0; ///< one past the piece's last byte in text()
        std::string text;      ///< the piece's own text, as drawn
    };

    /// One laid-out line: its box, its range of text() and its range of fragments().
    struct Line
    {
        float       top            = 0.0f;  ///< top edge in layout coordinates, margins included
        float       height         = 0.0f;  ///< line box height in DIPs
        float       left           = 0.0f;  ///< where the first fragment starts
        float       width          = 0.0f;  ///< content width, a trailing space excluded
        std::size_t begin          = 0;     ///< offsets into text(): the line's characters
        std::size_t end            = 0;     ///< one past its last, a hard break's `\n` excluded
        std::size_t next           = 0;     ///< where the next line starts
        std::size_t fragment_begin = 0;     ///< first index into fragments()
        std::size_t fragment_end   = 0;     ///< one past the last index into fragments()
        std::size_t paragraph      = 0;     ///< index into content()
        bool        hard_break     = false; ///< ended by `\n` or the paragraph's end
    };

    /// Empty: no content, no lines.
    TextLayout() = default;
    /// A copy of the content and the last layout, indexing its own copy of the runs.
    TextLayout(const TextLayout& other);
    /// As the copy constructor.
    TextLayout& operator=(const TextLayout& other);
    /// Moves; the runs stay where they are, so the index moves along.
    TextLayout(TextLayout&&) noexcept = default;
    /// As the move constructor.
    TextLayout& operator=(TextLayout&&) noexcept = default;
    ~TextLayout()                                = default;

    /// Replaces the content. Measurements are kept when only colors or tags changed, and
    /// everything is kept when nothing did. Returns whether the text or its formatting changed.
    bool set_content(std::vector<TextLayoutParagraph> paragraphs);

    /// The paragraphs from the last set_content().
    [[nodiscard]] const std::vector<TextLayoutParagraph>& content() const
    {
        return paragraphs_;
    }

    /// Lays the content out from `start` (an offset into text(); an overflow container's
    /// continuation), dropping whatever came before it. A start inside a paragraph drops that
    /// paragraph's top margin.
    void layout(const TextLayoutOptions& options, const TextMeasurer& measurer, std::size_t start = 0);

    /// The widest line (with its paragraph's margins) and every line's height with the margins.
    [[nodiscard]] Size size() const
    {
        return size_;
    }
    /// The lines of the last layout(), top to bottom.
    [[nodiscard]] const std::vector<Line>& lines() const
    {
        return lines_;
    }
    /// Every drawn piece of the last layout(), grouped per line (see Line::fragment_begin).
    [[nodiscard]] const std::vector<Fragment>& fragments() const
    {
        return fragments_;
    }
    /// The whole content as one plain UTF-8 string: runs in order, paragraphs joined by `\n`.
    /// Every offset this class takes or returns indexes into it.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Byte length of text().
    [[nodiscard]] std::size_t length() const
    {
        return text_.size();
    }
    /// Run `index` of the paragraphs' runs, flattened in order (Fragment::run indexes this).
    [[nodiscard]] const TextLayoutRun& run(std::size_t index) const
    {
        return *runs_[index].run;
    }
    /// Number of runs across all paragraphs.
    [[nodiscard]] std::size_t run_count() const
    {
        return runs_.size();
    }
    /// The `start` offset the last layout() was given; 0 unless this is an overflow continuation.
    [[nodiscard]] std::size_t start() const
    {
        return start_;
    }

    /// How many lines from the first fit whole in `height`: the lines a height-limited block
    /// shows before the rest overflows. May be 0.
    [[nodiscard]] std::size_t fitting_lines(float height) const;
    /// The height the first `count` lines take, their paragraphs' margins included.
    [[nodiscard]] float height_of(std::size_t count) const;

    /// The caret offset nearest `p` (layout coordinates): the line under it (the first above
    /// the text, the last below), then the nearer edge of the character under it.
    [[nodiscard]] std::size_t hit_test(Point p) const;
    /// The caret at `offset`: a zero-width rect as tall as its line. An offset where a line wraps
    /// is drawn at the start of the next line.
    [[nodiscard]] Rect caret_rect(std::size_t offset) const;
    /// The rects [begin, end) covers, one per line it touches.
    [[nodiscard]] std::vector<Rect> range_rects(std::size_t begin, std::size_t end) const;
    /// The tag of the run under `p`; unset when there is none or `p` is between runs.
    [[nodiscard]] std::optional<std::size_t> tag_at(Point p) const;
    /// The index of the line holding `offset` (as caret_rect() places it).
    [[nodiscard]] std::size_t line_of(std::size_t offset) const;
    /// The offset on line `line` nearest the horizontal position `x`.
    [[nodiscard]] std::size_t offset_at_x(std::size_t line, float x) const;

    /// Per-draw state for render(): colors, the visible line window, highlights and selection.
    struct Paint
    {
        /// Color of runs that don't set their own.
        Color foreground{};
        /// Only lines [first_line, last_line) are drawn, shifted up so first_line's top sits at
        /// the origin; an unset last_line draws to the end. This is how a block shows its part
        /// before an overflow takes over.
        std::size_t                first_line = 0;
        std::optional<std::size_t> last_line{};
        /// Drawn under the selection, each background first, then its foreground over its text.
        /// A non-owning view; the highlights must outlive the render() call.
        std::span<const TextHighlight> highlights{};
        /// The selection, drawn last over the highlights; unset draws none.
        std::optional<TextHighlight> selection{};
        /// Colors for ranges of the text, sorted by `begin` and not overlapping: the runs' text is
        /// drawn in pieces at their edges. Cheaper than foreground highlights for many ranges. A
        /// non-owning view; the spans must outlive the render() call.
        std::span<const TextColorSpan> colors{};
        /// Overrides a run's color by its tag (hyperlink hover and press); called for runs with
        /// a tag, and nullopt keeps the run's color.
        Callback<std::optional<Color>(std::size_t tag) const> tag_color{};
    };

    /// Draws the laid-out lines with their top-left at `origin`: highlight and selection
    /// backgrounds first, then the text (one draw_text() per run per line, one per word under
    /// Justify), then the recolored highlighted/selected text clipped to its range.
    void render(Renderer& renderer, Point origin, const Paint& paint) const;

private:
    struct RunRef
    {
        const TextLayoutRun* run       = nullptr;
        std::size_t          begin     = 0; // offset of the run's first character in text_
        std::size_t          paragraph = 0;
    };

    // A stretch of one run between break opportunities (or a hard break on its own).
    struct Piece
    {
        std::size_t run         = 0;
        std::size_t begin       = 0;
        std::size_t end         = 0;
        std::size_t trim_end    = 0; // end without trailing spaces
        float       width       = 0.0f;
        float       trim_width  = 0.0f;
        float       height      = 0.0f;
        bool        break_after = false; // a line may break after it
        bool        hard_break  = false; // the `\n` itself
        float       x           = 0.0f;  // placed position, set by layout()
    };

    [[nodiscard]] Size  measure_range(std::size_t run, std::size_t begin, std::size_t end) const;
    [[nodiscard]] float x_in_line(const Line& line, std::size_t offset) const;
    void                rebuild_index();

    std::vector<TextLayoutParagraph> paragraphs_;
    std::vector<RunRef>              runs_;
    std::vector<std::size_t>         paragraph_begin_; // offset of each paragraph's first character
    std::string                      text_;

    std::vector<Line>     lines_;
    std::vector<Fragment> fragments_;
    Size                  size_{};
    std::size_t           start_ = 0;

    // Measurement cache: a style and a stretch of text -> its size. Keyed by content, so an edit
    // keeps every measurement it didn't touch; dropped when the measurer changes or it grows big.
    struct StringHash
    {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };
    struct StyleCache
    {
        TextStyle                                                          style;
        std::unordered_map<std::string, Size, StringHash, std::equal_to<>> sizes;
    };
    mutable std::vector<StyleCache> cache_;
    mutable std::size_t             cache_entries_ = 0;
    // The run measure_range() last looked up, and its entry in cache_.
    mutable std::size_t         cached_run_    = detail::NoIndex;
    mutable std::size_t         cached_bucket_ = 0;
    mutable const TextMeasurer* measurer_      = nullptr;
};

} // namespace ui

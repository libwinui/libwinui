// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// One run of inline text with its own formatting. Every formatting field left at its default
/// inherits the containing block's.
///
/// An inline with `on_click` or `navigate_uri` is a link. It is underlined and drawn in the theme's
/// link colors. A press and release on it calls `on_click`, then opens `navigate_uri`. Inlines go
/// to TextControl::set_inlines() or `ui::Text::inlines`, or into a Paragraph for RichText.
///
/// Example:
/// @code
/// window.set_content(ui::Text{
///     .inlines = {
///         {.text = "Text can be "},
///         {.text = "bold", .weight = ui::FontWeight::Bold},
///         {.text = ", "},
///         {.text = "underlined", .underline = true},
///         ui::line_break(),
///         {.text = "or a link", .on_click = [] { std::println("clicked"); }, .navigate_uri = "https://example.com/"},
///     },
/// });
/// @endcode
struct Inline
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The run's text (UTF-8). `\n` is a line break.
    std::string text;
    /// The font family; unset inherits.
    std::optional<std::string> font_family{};
    /// The font size in DIPs; unset inherits.
    OptionalFloat font_size{};
    /// The font weight; unset inherits.
    std::optional<FontWeight> weight{};
    /// Italic. Only switches italic on; an italic block stays italic.
    bool italic = false;
    /// Underlined.
    bool underline = false;
    /// Struck through.
    bool strikethrough = false;
    /// The text color; unset inherits.
    std::optional<Color> color{};
    /// Makes the inline a link that calls this when clicked. It is a std::function because
    /// inlines are brace-initialized in vectors, which copy them (see spec/Common.hpp's handler
    /// note).
    std::function<void()> on_click{};
    /// Makes the inline a link that opens `navigate_uri` in its default app when clicked, after
    /// `on_click`.
    std::string navigate_uri{};

    /// Whether this inline is a link, which means it has `on_click` or a `navigate_uri`.
    [[nodiscard]] bool is_link() const
    {
        return static_cast<bool>(on_click) || !navigate_uri.empty();
    }
};

/// A line break: an Inline holding only `\n`.
[[nodiscard]] inline Inline line_break()
{
    return Inline{.text = "\n"};
}

/// Inlines that flow together as one block, with a margin around it.
///
/// The blocks of a RichText (RichTextControl::set_blocks(), `ui::RichText::blocks`).
///
/// Example:
/// @code
/// window.set_content(ui::RichText{
///     .blocks = {
///         {.inlines = {{.text = "First paragraph."}}},
///         {.inlines = {{.text = "Second, "}, {.text = "emphasized", .italic = true}},
///          .margin  = ui::Edges{.top = 12.0f}},
///     },
/// });
/// @endcode
struct Paragraph
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The paragraph's runs, in order.
    std::vector<Inline> inlines;
    /// The margin around the paragraph, DIPs.
    Thickness margin{};
};

/// Ranges of a text block's content painted with their own background and, optionally,
/// foreground. Ranges are start and length offsets into the block's plain text (its runs in
/// order, paragraphs separated by one character).
///
/// Example:
/// @code
/// // Highlight "consectetur" (offset 28, 11 characters) in yellow.
/// window.set_content(ui::RichText{
///     .blocks            = {{.inlines = {{.text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit"}}}},
///     .text_highlighters = std::vector<ui::TextHighlighter>{
///         ui::TextHighlighter{.ranges = {{28, 11}}, .background = ui::Color::from_argb(0xFFFFFF00)}},
/// });
/// @endcode
struct TextHighlighter
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// A span of the plain text: `length` characters from offset `start`.
    struct Range
    {
        /// The first offset covered.
        std::size_t start = 0;
        /// How many offsets are covered.
        std::size_t length = 0;
    };
    /// The spans to paint.
    std::vector<Range> ranges;
    /// The background painted behind every range.
    Color background{};
    /// The text color inside the ranges; nullopt keeps each run's own.
    std::optional<Color> foreground{};
};

/// Turns `paragraphs` into the runs a TextLayout lays out, with `base` as the block's style. Each
/// run gets its inline's own formatting over `base`, and `character_spacing`. Links are underlined
/// and tagged with their index in document order. When `links` is given, it receives the links in
/// that order. Its pointers point into `paragraphs` and stay valid only as long as it does.
[[nodiscard]] std::vector<TextLayoutParagraph> to_layout_paragraphs(
    const std::vector<Paragraph>& paragraphs,
    const TextStyle&              base,
    int                           character_spacing,
    std::vector<const Inline*>*   links = nullptr);

class ContextMenuHost;
struct Theme;

/// Selection and link state over laid-out inline text, shared by TextControl, RichText and
/// RichTextOverflow. Offsets are the TextLayout's. A RichText and its overflow containers each have
/// their own layout over the same content, so they share one selection.
///
/// A building block for a custom text element. Keep one as a member and pass it the element's
/// pointer and key events together with its TextLayout. Redraw when it reports a change, and let it
/// fill the layout's paint when rendering.
///
/// Example:
/// @code
/// class Article : public ui::Element
/// {
/// protected:
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         const bool held = interaction_.pressed_link || interaction_.dragging;
///         if (interaction_.pointer(e, layout_, ui::Point{bounds().x, bounds().y}, 0, bounds(), true, links_))
///         {
///             mark_dirty(ui::DirtyFlags::Visual);
///         }
///         return held || interaction_.pressed_link || interaction_.dragging; // a link or selection press
///     }
///     bool key_override(const ui::KeyEvent& e) override
///     {
///         const bool acted = interaction_.key(e, layout_);
///         if (acted)
///         {
///             mark_dirty(ui::DirtyFlags::Visual);
///         }
///         return acted;
///     }
///     // Programmatic selection: interaction_.select(0, 5); interaction_.selected_text(layout_);
///
/// private:
///     ui::TextLayout                 layout_;
///     ui::TextInteraction            interaction_;
///     std::vector<const ui::Inline*> links_; // from to_layout_paragraphs()
/// };
/// @endcode
class TextInteraction
{
public:
    /// Where the selection started (the fixed end), as a layout offset.
    std::size_t anchor = 0;
    /// The moving end of the selection, as a layout offset; equal to `anchor` when nothing is
    /// selected.
    std::size_t caret = 0;
    /// Whether a selection drag is in progress.
    bool dragging = false;
    /// The index of the link under the pointer; unset when there is none.
    std::optional<std::size_t> hover_link{};
    /// The index of the link pressed and not yet released; unset when there is none.
    std::optional<std::size_t> pressed_link{};

    /// The selection as an ordered [begin, end) pair of layout offsets.
    [[nodiscard]] std::pair<std::size_t, std::size_t> range() const
    {
        return {(std::min)(anchor, caret), (std::max)(anchor, caret)};
    }
    /// Whether any text is selected.
    [[nodiscard]] bool has_selection() const
    {
        return anchor != caret;
    }
    /// Selects the layout offsets [begin, end); `end` becomes the caret.
    void select(std::size_t begin, std::size_t end)
    {
        anchor = begin;
        caret  = end;
    }

    /// Handles pointer input over `layout`. The top of the first drawn line sits at `origin`, and
    /// `first_line` is the layout line drawn there. A press on a link arms it, and a release on the
    /// same link invokes it. Elsewhere, when `selectable`, a press places the selection and a drag
    /// extends it. A double click selects the word, a triple click the paragraph, and Shift extends.
    /// `sole_view` means the layout is the content's only view, so a drag carries on past its
    /// edges. One of several linked views, such as a RichText and its overflow containers, only
    /// follows the pointer while it is over that view. Returns whether anything visible changed.
    bool pointer(const PointerEvent&               e,
                 const TextLayout&                 layout,
                 Point                             origin,
                 std::size_t                       first_line,
                 const Rect&                       bounds,
                 bool                              selectable,
                 const std::vector<const Inline*>& links,
                 bool                              sole_view = true);

    /// Ctrl+A selects everything, and Ctrl+C copies the selection to ui::clipboard(). Returns
    /// whether it acted.
    bool key(const KeyEvent& e, const TextLayout& layout);

    /// Shows the context menu of a selectable text block, with Copy and Select All.
    void context_menu(Point anchor_point, const TextLayout& layout, ContextMenuHost& host);

    /// The selected text as UTF-8 (empty without a selection).
    [[nodiscard]] std::string selected_text(const TextLayout& layout) const;

    /// Fills the paint's link colors, and its selection when `show_selection`. The paint keeps
    /// a pointer to this object and to `theme` for the duration of the render.
    void paint(TextLayout::Paint& paint, const Theme& theme, bool show_selection, std::optional<Color> highlight) const;

private:
    mutable const Theme* theme_ = nullptr;
};

} // namespace ui

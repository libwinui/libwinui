// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Inlines.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

class RichTextOverflowControl;

/// Read-only paragraphs of formatted inlines, wrapping by default, selectable, with links and
/// highlighters. Content that doesn't fit can flow into RichTextOverflow containers.
///
/// The content is set with set_blocks(), as Paragraphs of Inlines. Offsets, such as the selection
/// and TextHighlighter ranges, count in plain_text(). Selection is on by default. The user can
/// drag, double-click a word, triple-click a paragraph, press Ctrl+A and Ctrl+C, and use a context
/// menu with Copy and Select All. In a spec tree it is built by `ui::RichText`.
///
/// Example:
/// @code
/// auto  row      = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// auto* rich     = row->add(std::make_unique<ui::RichTextControl>());
/// auto* overflow = row->add(std::make_unique<ui::RichTextOverflowControl>()); // measured after it
/// rich->set_blocks({
///     {.inlines = {{.text = "RichText shows "}, {.text = "formatted", .weight = ui::FontWeight::Bold},
///                  {.text = " paragraphs. "}, {.text = "Links", .navigate_uri = "https://example.com/"},
///                  {.text = " work too."}}},
///     {.inlines = {{.text = "What does not fit the block's height goes on in the overflow."}},
///      .margin  = ui::Edges{.top = 12.0f}},
/// });
/// rich->set_text_alignment(ui::TextAlignment::Justify);
/// rich->set_width(200.0f);
/// rich->set_height(120.0f);
/// overflow->set_width(200.0f);
/// rich->set_overflow_target(overflow);
/// window.set_content(std::move(row));
/// @endcode
class RichTextControl final : public Element
{
public:
    /// An empty block: Wrap, Left alignment, selection enabled.
    RichTextControl() = default;
    /// Unlinks any overflow containers, which then show nothing.
    ~RichTextControl() override;
    RichTextControl(const RichTextControl&)            = delete;
    RichTextControl& operator=(const RichTextControl&) = delete;

    // ---- content ----
    /// The paragraphs, as set by set_blocks().
    [[nodiscard]] const std::vector<Paragraph>& blocks() const
    {
        return blocks_;
    }
    /// Replaces the content and clears the selection.
    void set_blocks(std::vector<Paragraph> blocks);

    /// The content as plain text: the runs in order, paragraphs separated by `\n`. Every offset
    /// (selection, highlighters) counts in this text.
    [[nodiscard]] std::string plain_text() const;

    // ---- formatting ----
    /// The base style every run derives from (font family, size, weight).
    [[nodiscard]] const TextStyle& text_style() const
    {
        return style_;
    }
    /// The base style every run derives from. Unset (the default) is the theme's body text.
    /// Once set, the theme's body style no longer applies.
    void set_text_style(TextStyle style);

    /// The color of runs without a color of their own: a fixed color or a theme resource.
    /// Unset follows the theme's text color.
    [[nodiscard]] const Paint& foreground() const
    {
        return foreground_;
    }
    /// Sets the color of runs without their own; an empty source follows the theme.
    void set_foreground(Paint color)
    {
        foreground_ = color;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether lines break at the width; Wrap by default.
    [[nodiscard]] TextWrapping wrapping() const
    {
        return wrapping_;
    }
    /// Sets the wrapping mode, for the block and its overflow containers.
    void set_wrapping(TextWrapping wrapping);

    /// The alignment of every paragraph, Justify included; Left by default.
    [[nodiscard]] TextAlignment text_alignment() const
    {
        return alignment_;
    }
    /// Sets the alignment, for the block and its overflow containers.
    void set_text_alignment(TextAlignment alignment);

    /// Extra space added after every character, in thousandths of an em; 0 by default.
    [[nodiscard]] int character_spacing() const
    {
        return character_spacing_;
    }
    /// Sets the character spacing, for the block and its overflow containers.
    void set_character_spacing(int spacing);

    // ---- selection ----
    /// Whether the user can select and copy the text; true by default. While on, the block is a
    /// tab stop.
    [[nodiscard]] bool selection_enabled() const
    {
        return selection_enabled_;
    }
    /// Turns selection on or off. Either change clears the selection.
    void set_selection_enabled(bool enabled);

    /// The fixed color behind selected text; nullopt (the default) when unset or a theme resource
    /// is set. Unset is the accent.
    [[nodiscard]] std::optional<Color> selection_highlight_color() const
    {
        return selection_color_.color();
    }
    /// The color behind selected text as a theme resource, or nullptr.
    [[nodiscard]] ThemeColor selection_highlight_resource() const
    {
        return selection_color_.resource();
    }
    /// Sets the fixed color behind selected text and clears a theme resource; nullopt goes back
    /// to the accent.
    void set_selection_highlight_color(std::optional<Color> color)
    {
        selection_color_ = color;
        mark_chain_dirty();
    }
    /// Sets the color behind selected text to a theme resource, re-read under every theme, and
    /// clears a fixed one.
    void set_selection_highlight_color(ThemeColor resource)
    {
        selection_color_ = resource;
        mark_chain_dirty();
    }

    /// The selection as [start, end) offsets into plain_text(); empty when start == end.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection() const
    {
        return interaction_.range();
    }
    /// Selects [start, end) of plain_text(). The selection shows only while the block, or one of
    /// its overflow containers, has focus.
    void select(std::size_t start, std::size_t end)
    {
        interaction_.select(start, end);
        mark_chain_dirty();
    }
    /// Selects the whole plain_text() (Ctrl+A).
    void select_all();
    /// The selected text; what Ctrl+C copies.
    [[nodiscard]] std::string selected_text() const
    {
        return interaction_.selected_text(layout_);
    }

    /// The highlighters painted over the text.
    [[nodiscard]] const std::vector<TextHighlighter>& text_highlighters() const
    {
        return highlighters_;
    }
    /// Replaces every highlighter.
    void set_text_highlighters(std::vector<TextHighlighter> highlighters)
    {
        highlighters_ = std::move(highlighters);
        mark_chain_dirty();
    }

    // ---- overflow ----
    /// The first overflow container (non-owning), or nullptr.
    [[nodiscard]] RichTextOverflowControl* overflow_target() const
    {
        return overflow_target_;
    }
    /// Sets where content that does not fit continues. The overflow must be in the same tree and
    /// measured after this block, such as a later sibling or a later cell of the same Grid. nullptr
    /// unlinks it.
    void set_overflow_target(RichTextOverflowControl* target);
    /// Whether some of the content did not fit and went on to the overflow target.
    [[nodiscard]] bool has_overflow_content() const
    {
        return overflow_start_ != ui::detail::NoIndex;
    }

    /// A tab stop while text selection is enabled.
    [[nodiscard]] bool focusable() const override
    {
        return selection_enabled_;
    }
    /// Whether this block itself holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Copy and Select All, while selection is enabled.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- automation ----
    /// A hand over a link, an I-beam over selectable text.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override;
    /// AutomationRole::Document.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Document;
    }
    /// plain_text().
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return plain_text();
    }
    /// One node per link in the lines this block shows. Invoking a node follows its link.
    void automation_children(std::vector<Element*>& out) const override;

protected:
    // ---- Element ----
    /// Lays the content out at the offered width (per wrapping()); with an overflow
    /// target, keeps only the whole lines that fit the offered height.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Re-wraps at the final width when it changed, and re-places the lines for the final
    /// height, which decides where the overflow picks up.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// The lines this block keeps, with its highlighters and the selection (while the chain
    /// has focus).
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Drag to select, double-click a word or triple-click a paragraph. A click on a link
    /// follows it.
    bool pointer_override(const PointerEvent& e) override;


private:
    friend class RichTextOverflowControl;

    void mark_chain_dirty();
    // Whether the block or any overflow container in its chain has focus: the selection shows.
    [[nodiscard]] bool chain_focused() const;
    void               place_lines(float height) const; // shown_ and overflow_start_ for `height`
    void               paint(TextLayout::Paint& paint, const Theme& theme) const;

    std::vector<Paragraph> blocks_;

    TextStyle     style_{14.0f, TextAlign::Leading, TextAlign::Center, "Segoe UI"};
    bool          style_explicit_ = false;
    Paint         foreground_{};
    TextWrapping  wrapping_          = TextWrapping::Wrap;
    TextAlignment alignment_         = TextAlignment::Left;
    int           character_spacing_ = 0;

    bool                         selection_enabled_ = true;
    Paint                        selection_color_{};
    std::vector<TextHighlighter> highlighters_;
    TextInteraction              interaction_;

    RichTextOverflowControl* overflow_target_ = nullptr;

    mutable TextLayout                 layout_;
    mutable TextLayoutOptions          layout_options_{};
    mutable const TextMeasurer*        measurer_ = nullptr;
    mutable const Theme*               theme_    = nullptr;
    mutable std::vector<const Inline*> links_;
    // One automation node per place in links_, made on first use and kept for the control's life.
    mutable std::vector<std::unique_ptr<Element>> link_automation_;
    mutable std::size_t                           shown_          = 0;                   // lines this block draws
    mutable std::size_t                           overflow_start_ = ui::detail::NoIndex; // where the overflow picks up

protected:
    /// Called by Window as focus moves. The selection shows while the chain has focus.
    void focus_changed(bool /*focused*/) override
    {
        mark_chain_dirty();
    }
    /// Ctrl+A selects all, Ctrl+C copies the selection.
    bool key_override(const KeyEvent& e) override;

protected:
    /// None: selectable text shows focus through its selection.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }
};

/// Shows the part of a RichText's content that did not fit the block or the previous overflow
/// container, laid out at its own width. Link one with RichTextControl::set_overflow_target(),
/// and chain another one with this one's set_overflow_target(). It has no content of its own, so
/// an unlinked overflow is empty.
///
/// The links don't own anything. Each container is owned by the tree like any element and must
/// be measured after the one before it. Destroying one in the chain unlinks everything after it.
/// Selection, copying and links work here as in the RichText, on the RichText's own selection.
///
/// Example:
/// @code
/// const std::string long_article_text = "An article longer than one column holds...";
/// auto  columns = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// auto* rich    = columns->add(std::make_unique<ui::RichTextControl>());
/// auto* second  = columns->add(std::make_unique<ui::RichTextOverflowControl>());
/// auto* third   = columns->add(std::make_unique<ui::RichTextOverflowControl>());
/// rich->set_blocks({{.inlines = {{.text = long_article_text}}}});
/// const auto column = [](ui::Element* e) { e->set_width(180.0f); e->set_height(240.0f); };
/// column(rich);
/// column(second);
/// column(third);
/// rich->set_overflow_target(second);
/// second->set_overflow_target(third);
/// window.set_content(std::move(columns));
/// @endcode
class RichTextOverflowControl final : public Element
{
public:
    /// An unlinked, empty container.
    RichTextOverflowControl() = default;
    /// Unlinks itself; the containers after it lose their source.
    ~RichTextOverflowControl() override;
    RichTextOverflowControl(const RichTextOverflowControl&)            = delete;
    RichTextOverflowControl& operator=(const RichTextOverflowControl&) = delete;

    /// The RichText whose content this shows, nullptr while unlinked.
    [[nodiscard]] RichTextControl* content_source() const
    {
        return master_;
    }
    /// The next container in the chain (non-owning), or nullptr.
    [[nodiscard]] RichTextOverflowControl* overflow_target() const
    {
        return next_;
    }
    /// Sets where this container's leftover goes. Same rules as
    /// RichTextControl::set_overflow_target(); nullptr unlinks it.
    void set_overflow_target(RichTextOverflowControl* target);
    /// Whether some content still did not fit here and went on to the next container.
    [[nodiscard]] bool has_overflow_content() const
    {
        return overflow_start_ != ui::detail::NoIndex;
    }
    /// The offset into the RichText's plain text this container starts at (npos while it shows
    /// nothing).
    [[nodiscard]] std::size_t content_start() const
    {
        return start_;
    }

    /// A tab stop while linked, showing something, and the RichText allows selection.
    [[nodiscard]] bool focusable() const override
    {
        return master_ && master_->selection_enabled_ && start_ != ui::detail::NoIndex;
    }
    /// The RichText's Copy/Select All menu.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    /// As the RichText: a hand over a link, an I-beam over selectable text.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override;
    /// AutomationRole::Document.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Document;
    }

protected:
    /// Lays the rest of the RichText's content out at the offered width, keeping the whole
    /// lines that fit the offered height (all of it when nothing is linked after it).
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Re-places the lines for the final height.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// The lines this container shows, in the RichText's formatting, highlighters and selection.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Selection and links, as in the RichText, acting on the RichText's selection.
    bool pointer_override(const PointerEvent& e) override;


private:
    friend class RichTextControl;

    void                      attach(RichTextControl* master, RichTextOverflowControl* previous);
    void                      detach(); // this and everything after it lose their source
    [[nodiscard]] std::size_t previous_overflow_start() const;
    void                      place_lines(float height) const;

    RichTextControl*         master_   = nullptr;
    RichTextOverflowControl* previous_ = nullptr; // nullptr: directly after the master
    RichTextOverflowControl* next_     = nullptr;

    mutable TextLayout  layout_;
    mutable std::size_t start_          = ui::detail::NoIndex;
    mutable std::size_t shown_          = 0;
    mutable std::size_t overflow_start_ = ui::detail::NoIndex;

protected:
    /// Called by Window as focus moves. The selection shows while the chain has focus.
    void focus_changed(bool /*focused*/) override
    {
        if (master_)
        {
            master_->mark_chain_dirty();
        }
    }
    /// Ctrl+A and Ctrl+C, as the RichText's.
    bool key_override(const KeyEvent& e) override;

protected:
    /// None: selectable text shows focus through its selection.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }
};

struct RichTextOverflow;

/// Paragraphs of formatted inlines. Builds a ui::RichTextControl.
///
/// Read-only, selectable by default and wrapping by default. Content that does not fit can flow
/// on into a ui::RichTextOverflow.
///
/// Example:
/// @code
/// window.set_content(ui::RichText{
///     .blocks = {
///         { .inlines = { { .text = "I am a " },
///                        { .text = "RichText", .weight = ui::FontWeight::Bold },
///                        { .text = " with an ", .italic = true },
///                        { .text = "italic run." } } },
///         { .inlines = { { .text = "A second paragraph." } }, .margin = ui::Edges{.top = 8} },
///     },
///     .max_width = 400,
/// });
/// @endcode
struct RichText
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<RichText>` points at.
    using control_type = RichTextControl;

    // ---- properties ----
    /// The content: paragraphs of inlines. Space paragraphs apart with each Paragraph's `margin`.
    Binding<std::vector<Paragraph>> blocks{};
    /// The user can select the text and copy it; on by default.
    Binding<bool> selection_enabled{true};
    /// Ranges painted with their own background (and foreground).
    Binding<std::vector<TextHighlighter>> text_highlighters{std::vector<TextHighlighter>{}};
    /// Where the content that does not fit continues, by the RichTextOverflow's handle
    /// (RichTextControl::set_overflow_target()).
    ControlLink<RichTextOverflow> overflow{};
    /// Base style for every run; unset follows the theme's body text.
    Binding<std::optional<TextStyle>> text_style{};
    /// Whether lines break at the available width; Wrap by default.
    Binding<TextWrapping> wrapping{TextWrapping::Wrap};
    /// How each line sits between the left and right edges.
    Binding<TextAlignment> text_alignment{TextAlignment::Left};
    /// Extra space between characters, in thousandths of an em.
    Binding<int> character_spacing{0};
    /// The color behind selected text: a fixed color or a theme resource, which follows theme
    /// switches. Unset is the accent.
    Binding<Paint> selection_highlight_color{};
    /// The color of runs without a color of their own: a fixed color or a theme resource. Unset
    /// uses the theme's text color.
    Binding<Paint> foreground{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<RichText> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::RichTextControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// Where a RichText's content continues when it does not fit.
/// Builds a ui::RichTextOverflowControl, named by the RichText's `.overflow`, or by the previous
/// RichTextOverflow's for a chain. It has no content of its own. It shows whatever of its
/// source's paragraphs the previous container in the chain could not fit.
///
/// Example:
/// @code
/// const std::string                 long_text = "A text longer than one column holds...";
/// ui::Control<ui::RichTextOverflow> more;
///
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 12,
///     .height   = 300,
///     .content  = {
///         ui::RichText{ .blocks = {{ .inlines = {{ .text = long_text }} }}, .overflow = more, .width = 240 },
///         ui::RichTextOverflow{ .width = 240, .control = more }, // continues where the text stops
///     },
/// });
/// @endcode
struct RichTextOverflow
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<RichTextOverflow>` points at.
    using control_type = RichTextOverflowControl;

    // ---- properties ----
    /// The next container of the chain, by its handle (RichTextOverflowControl::set_overflow_target()).
    ControlLink<RichTextOverflow> overflow{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    /// Out: a non-owning observer of the built control; needed to link it into a chain.
    ControlOut<RichTextOverflow> control{};

    /// Consumes the spec and returns the built ui::RichTextOverflowControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

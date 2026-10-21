// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
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

#include <cstddef>
#include <cstdint>

namespace ui
{

/// What a TextBox's `on_edit` gets: `removed` at `position` became `inserted`. Byte offsets into
/// the text before the change; the views are valid during the call.
struct TextBoxEditArgs
{
    std::size_t      position = 0; ///< Where the change starts.
    std::string_view removed;      ///< The text that was there.
    std::string_view inserted;     ///< The text that is there now.
};

/// Single- or multi-line editable text.
///
/// A focusable field with a blinking caret, mouse and keyboard selection, and Ctrl+C/X/V through
/// the host's Clipboard. When the host provides a SpellChecker, misspellings get squiggles and a
/// context menu of suggestions. It can show a header above the field and a placeholder while empty.
/// set_accepts_return() or set_wrapping() make it multi-line. A multi-line box grows a line at a
/// time and scrolls when it is shorter than its text. set_password_char() makes it PasswordBox's
/// edit part. on_text_changed() reports user edits only, never set_text(). In a spec tree it is
/// built by `ui::TextBox`.
///
/// Example:
/// @code
/// ui::Property<std::string> name{"Ada"};
/// ui::Control<ui::TextBox> notes;
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::TextBox{.header           = "Name",
///                     .placeholder_text = "Your name",
///                     .text             = name,
///                     .on_submit        = [&] { /* save name.get() */ }},
///         ui::TextBox{.header         = "Notes",
///                     .accepts_return = true,
///                     .wrapping       = ui::TextWrapping::Wrap,
///                     .height         = 120,
///                     .control        = notes},
///         ui::Text{.text = ui::bind(name, [](const std::string& n) { return "Hello, " + n; })},
///     },
/// });
///
/// // Or imperatively:
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* code  = stack->add(std::make_unique<ui::TextBoxControl>());
/// code->set_header("Activation code");
/// code->set_max_length(8);
/// code->set_character_casing(ui::CharacterCasing::Upper);
/// code->set_spell_check(false);
/// code->on_text_changed([](const std::string& text) { /* validate */ });
/// @endcode
class TextBoxControl final : public Element, private ValueProvider
{
public:
    /// An empty, enabled, single-line box.
    TextBoxControl();
    /// A box holding `text` (UTF-8), the caret at its end.
    explicit TextBoxControl(std::string text);

    // ---- content ----
    /// The edited string, UTF-8.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Replaces the text and moves the caret to the end. Setting the same text again changes
    /// nothing, caret and selection included, so a two-way binding writing the value back is
    /// harmless. Doesn't fire on_text_changed(), which reports user edits only.
    void set_text(std::string text);

    /// The caret position, a byte offset into text().
    [[nodiscard]] std::size_t caret() const
    {
        return caret_;
    }
    /// The selected range as [start, end) byte offsets; start == end means no selection.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection_range() const;
    /// Whether selection_range() is non-empty.
    [[nodiscard]] bool has_selection() const;

    /// The header text; empty when there is none.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the control. Empty (the default) draws none and takes no room.
    void set_header(std::string header)
    {
        if (header_.set_text(std::move(header)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The secondary text under the box; empty (the default) draws none.
    [[nodiscard]] const std::string& description() const
    {
        return description_.text();
    }
    /// Sets the description. Empty draws none and takes no room.
    void set_description(std::string description)
    {
        if (description_.set_text(std::move(description)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// What is wrong with the value, shown in the description's place in the critical color,
    /// with the border in the same color; empty (the default) shows the description.
    [[nodiscard]] const std::string& error() const
    {
        return description_.error();
    }
    /// Sets the error. Empty shows the description again.
    void set_error(std::string error)
    {
        if (description_.set_error(std::move(error)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The description, or the error while one is set, is the help text a screen reader offers
    /// when accessibility().description is empty.
    [[nodiscard]] std::string automation_help_text_fallback() const override
    {
        return description_.shown();
    }
    /// Invalid while an error is set.
    [[nodiscard]] bool automation_invalid() const override
    {
        return description_.has_error();
    }
    /// The header names the control when accessibility().name does not.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }


    /// Whether the pointer is over the box, which draws it in its hover colors.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }

    /// The placeholder hint; empty (the default) draws none.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return placeholder_text_;
    }
    /// Whether measure applies the theme's minimum text box height (32 by default). On for a
    /// TextBox of its own, off for one serving as another control's edit part (ComboBox,
    /// NumberBox), which sizes itself.
    void set_uses_theme_min_height(bool uses)
    {
        theme_min_height_ = uses;
        mark_dirty(DirtyFlags::Layout);
    }

    /// A hint drawn in a secondary text color while text() is empty, focused or not. Never
    /// part of text(), never measured into the desired size.
    void set_placeholder_text(std::string text)
    {
        placeholder_text_ = std::move(text);
        mark_dirty(DirtyFlags::Visual);
    }

    /// A chromeless box draws no background fill, border stroke or focus underline, only the
    /// text, selection and caret, so a host control (ComboBox) can paint one background and
    /// border under its whole surface.
    void set_chromeless(bool chromeless)
    {
        chromeless_ = chromeless;
    }

    // ---- formatting ----
    /// The font family override; unset (the default) follows the theme.
    [[nodiscard]] const std::optional<std::string>& font_family() const
    {
        return font_family_;
    }
    /// Sets or clears the font family; unset follows the theme.
    void set_font_family(std::optional<std::string> family)
    {
        font_family_ = std::move(family);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The font size override in DIPs; unset (the default) follows the theme.
    [[nodiscard]] std::optional<float> font_size() const
    {
        return font_size_;
    }
    /// Sets or clears the font size; unset follows the theme. The box grows with it.
    void set_font_size(OptionalFloat size)
    {
        font_size_ = size;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The font weight override; nullopt (the default) follows the theme.
    [[nodiscard]] std::optional<FontWeight> font_weight() const
    {
        return font_weight_;
    }
    /// Sets the font weight; nullopt follows the theme.
    void set_font_weight(std::optional<FontWeight> weight)
    {
        font_weight_ = weight;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether the text is italic; false by default.
    [[nodiscard]] bool italic() const
    {
        return italic_;
    }
    /// Italic text.
    void set_italic(bool italic)
    {
        italic_ = italic;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The extra space between characters in thousandths of an em; 0 by default.
    [[nodiscard]] int character_spacing() const
    {
        return character_spacing_;
    }
    /// Sets the extra space between characters, in thousandths of an em.
    void set_character_spacing(int spacing)
    {
        character_spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The text color: a fixed color or a theme resource; empty (the default) follows the theme.
    [[nodiscard]] const Paint& foreground() const
    {
        return foreground_;
    }
    /// The text's color; unset follows the theme. The placeholder keeps its own.
    void set_foreground(Paint color)
    {
        foreground_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
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
        mark_dirty(DirtyFlags::Visual);
    }
    /// Sets the color behind selected text to a theme resource, re-read under every theme, and
    /// clears a fixed one.
    void set_selection_highlight_color(ThemeColor resource)
    {
        selection_color_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The color behind selected text while the box doesn't have focus, as a fixed color or a
    /// theme resource. Empty (the default) hides the selection then.
    [[nodiscard]] const Paint& unfocused_selection_highlight_color() const
    {
        return unfocused_selection_color_;
    }
    /// Shows the selection in `color` while the box doesn't have focus, with the text over it
    /// unchanged. A find bar uses this to keep the match it selected visible. nullopt hides it again.
    void set_unfocused_selection_highlight_color(std::optional<Color> color)
    {
        unfocused_selection_color_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The theme-resource form of set_unfocused_selection_highlight_color().
    void set_unfocused_selection_highlight_color(ThemeColor resource)
    {
        unfocused_selection_color_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The style the text is measured and drawn in: the theme's, under the font properties above.
    [[nodiscard]] TextStyle text_style(const Theme& theme) const;

    // ---- behavior ----
    /// Whether the text is read-only; false by default.
    [[nodiscard]] bool read_only() const
    {
        return read_only_;
    }
    /// The text can be selected and copied but not changed, and no caret is drawn.
    void set_read_only(bool read_only);

    /// Whether Enter inserts a line break; false by default.
    [[nodiscard]] bool accepts_return() const
    {
        return accepts_return_;
    }
    /// Enter inserts a line break instead of submitting, and pasted line breaks are kept. Makes
    /// the box multi-line.
    void set_accepts_return(bool accepts)
    {
        accepts_return_ = accepts;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether lines break at the box's width; NoWrap by default.
    [[nodiscard]] TextWrapping wrapping() const
    {
        return wrapping_;
    }
    /// Anything but NoWrap breaks the text at the box's width and makes it
    /// multi-line.
    void set_wrapping(TextWrapping wrapping)
    {
        wrapping_ = wrapping;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether the text is laid out as lines, which accepts_return() or wrapping turns on.
    [[nodiscard]] bool multiline() const
    {
        return accepts_return_ || wrapping_ != TextWrapping::NoWrap;
    }
    /// Whether Tab inserts a tab character; false by default.
    [[nodiscard]] bool accepts_tab() const
    {
        return accepts_tab_;
    }
    /// Tab inserts a tab character instead of moving focus, while the box is editable. Shift+Tab
    /// then does nothing. An `on_key_down` handler can take both keys to indent and outdent.
    void set_accepts_tab(bool accepts)
    {
        accepts_tab_ = accepts;
    }
    /// While accepts_tab() and editable.
    [[nodiscard]] bool takes_tab_key() const override
    {
        return accepts_tab_ && editable();
    }

    // ---- code ----
    /// Colors for ranges of the text; empty by default.
    [[nodiscard]] const std::vector<TextColorSpan>& color_spans() const
    {
        return color_spans_;
    }
    /// Draws ranges of the text in their own colors, as a syntax highlighter does. The selection
    /// still draws over them. Ranges are byte offsets. The spans are sorted and overlaps are cut.
    /// An edit moves the spans after it and stretches or cuts the ones it touches, so they stay on
    /// their text until the next call. Multi-line boxes only.
    void set_color_spans(std::vector<TextColorSpan> spans);
    /// Whether a multi-line box numbers its lines; false by default.
    [[nodiscard]] bool line_numbers() const
    {
        return line_numbers_;
    }
    /// Shows a column of line numbers left of a multi-line box's text, one per line of the text
    /// (a wrapped line keeps one number).
    void set_line_numbers(bool shown)
    {
        line_numbers_ = shown;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The most characters the user can enter, in code points; 0 (the default) is no limit.
    [[nodiscard]] std::size_t max_length() const
    {
        return max_length_;
    }
    /// The most characters (code points) the user can type or paste; 0, the default, is no limit.
    /// A paste is cut to what fits. Text already in the box or set from code is left alone, even
    /// when it is longer.
    void set_max_length(std::size_t length)
    {
        max_length_ = length;
    }
    /// How typed text is cased; Normal (keep as typed) by default.
    [[nodiscard]] CharacterCasing character_casing() const
    {
        return casing_;
    }
    /// What the user types or pastes from now on is lower- or upper-cased. Text already in the box
    /// keeps its case.
    void set_character_casing(CharacterCasing casing)
    {
        casing_ = casing;
    }

    /// Whether this box has a delete button at all. On for a TextBox of its own. A control that
    /// uses one as its edit part and shows no delete button (ComboBox) turns it off.
    void set_delete_button_enabled(bool enabled)
    {
        delete_button_enabled_ = enabled;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Room kept free at the right edge, after the delete button, for a button the owning control
    /// draws there itself, such as PasswordBox's reveal button or AutoSuggestBox's query button.
    /// The text never runs under it.
    void set_trailing_space(float width)
    {
        trailing_space_ = width;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The width in DIPs kept free by set_trailing_space(); 0 by default.
    [[nodiscard]] float trailing_space() const
    {
        return trailing_space_;
    }
    /// An I-beam over the text (the padding included), an arrow over the delete button and any
    /// button a containing control keeps in the trailing space.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override;
    /// Whether the delete button is shown and clickable right now. It shows while the box is
    /// single-line, editable, non-empty, focused and wider than five ems, under a theme that has
    /// one, unless set_delete_button_enabled() turned it off.
    [[nodiscard]] bool delete_button_visible() const;
    /// The delete button's rect (empty while it is hidden).
    [[nodiscard]] Rect delete_button_rect() const;

    /// Draws every character as `mask`, and turns off copying, cutting, spell checking and word
    /// selection, which a password must not offer. nullopt shows the text.
    void set_password_char(std::optional<char32_t> mask);
    /// The mask character; nullopt (the default) when the text is shown.
    [[nodiscard]] std::optional<char32_t> password_char() const
    {
        return password_char_;
    }

    /// Fired after any user-driven edit (typing, Backspace, Delete, cut, paste, undo and redo)
    /// and after replace_selection(), undo() and redo(), with the new text. set_text() raises
    /// nothing.
    void on_text_changed(Callback<void(const std::string&)> handler)
    {
        on_text_changed_ = std::move(handler);
    }
    /// Fired for every change to the text, with what changed: typing, pasting, undo and redo,
    /// replace_selection() and set_text() too. Runs right after the change, before on_text_changed()
    /// and before the caret moves. Don't edit the box from it. Use it to feed an incremental parser.
    void on_edit(Callback<void(const TextBoxEditArgs&)> handler)
    {
        on_edit_ = std::move(handler);
    }
    /// Fired when the user presses Enter, unless accepts_return() makes Enter insert a line break.
    void on_submit(Callback<void()> handler)
    {
        on_submit_ = std::move(handler);
    }
    /// Fired when the caret or the selection moved, by a key, the pointer, an edit or a call.
    void on_selection_changed(Callback<void()> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- selection and editing from code ----
    /// Selects from `anchor` to `caret` (byte offsets; the caret goes to `caret`, so an anchor after
    /// it selects backwards). Both are clamped to the text and moved back to a character boundary.
    /// The view scrolls to the caret.
    void select(std::size_t anchor, std::size_t caret);
    /// Selects the whole text.
    void select_all();
    /// The selected text; empty with nothing selected.
    [[nodiscard]] std::string selected_text() const;
    /// Replaces the selection with `text`, or inserts it at the caret, and puts the caret after it.
    /// An edit like a typed one: it can be undone and raises on_text_changed(). Neither read_only
    /// nor max_length stops it.
    void replace_selection(std::string_view text);

    // ---- undo ----
    /// Whether there is an edit undo() can take back. Ctrl+Z does the same.
    [[nodiscard]] bool can_undo() const
    {
        return !undo_.empty();
    }
    /// Whether there is an undone edit redo() can make again. Ctrl+Y and Ctrl+Shift+Z do the same.
    [[nodiscard]] bool can_redo() const
    {
        return !redo_.empty();
    }
    /// Takes back the last edit and puts the caret and the selection back where they were before
    /// it; nothing when can_undo() is false. Typing is undone a word at a time, a run of Backspace
    /// or Delete presses at once.
    void undo();
    /// Makes the last undone edit again; nothing when can_redo() is false.
    void redo();
    /// Forgets every edit, so undo() has nothing to take back. set_text() does this too, so a
    /// loaded document starts with an empty history.
    void clear_undo_history();
    /// How many edits undo() keeps; older ones drop out.
    static constexpr std::size_t MaxUndoSteps = 1000;

    // ---- layout ----
    /// True: measuring reads only the box's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- focus ----
    /// Whether the box has keyboard focus (caret shown, blink running).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Focusable while enabled (a read-only box too, for selecting and copying).
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- spell checking ----
    /// Whether spell checking is on; true by default. Without a SpellChecker from the host it
    /// does nothing either way.
    [[nodiscard]] bool spell_check() const
    {
        return spell_check_enabled_;
    }
    /// Turns spell checking on or off; turning it off drops every squiggle at once.
    void set_spell_check(bool enabled);

    /// The current misspellings, sorted by start. Includes the word being typed, which gets no
    /// squiggle until it's finished.
    [[nodiscard]] const std::vector<SpellingError>& spelling_errors() const
    {
        return spelling_errors_;
    }

    /// Idle time in ms after the last edit before the whole text is checked again.
    static constexpr double SpellCheckDelayMs = 250.0;
    /// The most spelling suggestions the context menu lists, best first.
    static constexpr std::size_t MaxSpellingSuggestions = 5;

    /// Keeps the host's SpellChecker and schedules a full check with it. The Window calls this as
    /// the box gains focus. The box doesn't own the checker.
    void sync_spell_checker(SpellChecker& checker) override;

    // ---- context menu ----
    /// Shows spelling suggestions, "Add to dictionary" and "Ignore" over a squiggle, then Cut,
    /// Copy, Paste and Select All, each enabled when it can act.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- time ----
    /// How long the caret stays shown, and then hidden, in ms. The user's blink setting is not read.
    static constexpr double CaretBlinkIntervalMs = 530.0;

    /// The current blink phase: whether a focused box draws its caret right now.
    [[nodiscard]] bool caret_visible() const
    {
        return caret_visible_;
    }

    // ---- automation ----
    /// Always AutomationRole::Edit.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Edit;
    }
    /// The caret, one DIP wide and a line tall, while focused and editable.
    [[nodiscard]] std::optional<Rect> caret_bounds() const override;

protected:
    /// As wide as the text or placeholder plus padding and buttons, at most `available.width`. As
    /// tall as its lines plus padding, at least the theme's minimum text box height while
    /// set_uses_theme_min_height() is on. The header and description add their own room.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the header and the field; a multi-line box re-flows its text to the new width.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Click to place the caret, drag / Shift+click to select, double-click a word, triple-click
    /// everything. The delete button clears the text.
    bool pointer_override(const PointerEvent& e) override;
    /// A multi-line box taller than its view scrolls on the wheel, and shows a scroll bar.
    bool wheel_override(const PointerEvent& e) override;

    /// Toggles caret visibility every CaretBlinkIntervalMs while focused.
    void tick_override(Duration delta) override;

    // ---- render ----
    /// Background, border and focus underline (unless chromeless), header, placeholder or text,
    /// selection highlight, squiggles, caret and delete button.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // The fields built around a TextBox answer key routing with its takes_*() below.
    friend class ComboBoxControl;
    friend class AutoSuggestBoxControl;
    friend class NumberBoxControl;
    friend class PasswordBoxControl;

    // An owner that shows its own error text under itself (NumberBox, AutoSuggestBox, ComboBox)
    // still has this box draw the critical border.
    void set_invalid_border(bool invalid)
    {
        if (invalid_border_ != invalid)
        {
            invalid_border_ = invalid;
            mark_dirty(DirtyFlags::Visual);
        }
    }
    bool invalid_border_ = false;

    // The editing keys and the clipboard chords are the field's, never an app-wide shortcut's
    // (Element::key_override()): a menu's Ctrl+C or a command's Delete waits until focus leaves it.
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const;
    // Left/Right drive the caret; a single-line field has no use for Up/Down, so those carry
    // focus out of it, while a multi-line one moves between its lines with them. Claimed so
    // Window's directional focus navigation leaves them alone (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight ||
               (multiline() && (key == Key::ArrowUp || key == Key::ArrowDown));
    }
    // Enter means something only to a field with a submit handler; otherwise it reaches a
    // dialog's default button (Element::key_override()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return static_cast<bool>(on_submit_) || (accepts_return_ && !read_only_);
    }
    // The width of text_'s first `count` bytes as drawn (masked, for a password).
    [[nodiscard]] float       prefix_width(std::size_t count, const TextStyle& style) const;
    [[nodiscard]] std::string masked(std::size_t code_points) const; // that many mask characters
    [[nodiscard]] std::size_t hit_test(float local_x) const;         // local_x relative to the text origin (left inset)
    [[nodiscard]] std::size_t hit_at(Point window_point) const;      // either mode
    [[nodiscard]] std::string display_text() const;
    [[nodiscard]] bool        editable() const
    {
        return is_enabled() && !read_only_;
    }
    [[nodiscard]] bool spelling_on() const;
    [[nodiscard]] Rect text_area() const;
    [[nodiscard]] Rect bar_viewport() const; // the vertical scroll bar's column, over the text's right edge
    void               sync_layout() const;
    // Lays the text out again when it, the width or the style changed since the last layout.
    void relayout_if_needed(const TextLayoutOptions& options) const;
    // Calls on_edit_ for a change already made to text_.
    void report_edit(std::size_t pos, std::string_view removed, std::string_view inserted);
    // The left edge of the text's first character, scrolled (multi-line).
    [[nodiscard]] Point text_origin() const;
    // The horizontal scroll bar's band under the text, and how far a NoWrap box can scroll.
    [[nodiscard]] Rect  hbar_viewport() const;
    [[nodiscard]] float max_scroll_x() const;
    void draw_line_numbers(Renderer& renderer, std::size_t first, std::size_t last, const Color& text_color) const;
    void insert_text(std::string_view text); // replaces the selection; no change events
    // What the user may insert in place of the selection: `text` in CharacterCasing's case, cut
    // to MaxLength's room in code points (never inside a cluster).
    [[nodiscard]] std::string admit(std::string_view text) const;
    void                      move_vertically(int lines, bool extend);
    void                      erase_selection();
    void                      changed(); // marks dirty + fires on_text_changed_

    // ---- undo ----
    // What a step of the history is, for joining runs of typing, Backspace or Delete into one.
    enum class EditKind
    {
        Other,
        Typing,
        Backspace,
        Delete,
    };
    // One edit: `removed` at `pos` became `inserted`, and where the caret and anchor were before.
    struct UndoStep
    {
        std::size_t pos = 0;
        std::string removed;
        std::string inserted;
        std::size_t caret_before  = 0;
        std::size_t anchor_before = 0;
        EditKind    kind          = EditKind::Other;
    };
    // Every change to text_ goes through here: replaces `count` bytes at `pos` with `text`,
    // reports it to note_edit() and records it for undo (joined to the last step when `kind`
    // continues it). The caller places the caret.
    void edit(std::size_t pos, std::size_t count, std::string_view text, EditKind kind = EditKind::Other);
    // Applies a step backwards (undo) or forwards (redo), outside the history.
    void                  apply_step(const UndoStep& step, bool backwards);
    std::vector<UndoStep> undo_;
    std::vector<UndoStep> redo_;
    bool                  joinable_ = false; // the next typing, Backspace or Delete may join undo_.back()

    // Raises on_selection_changed when the caret or anchor differ at the end of the scope, and
    // has the view follow the caret then.
    class SelectionWatch;
    friend class SelectionWatch;
    int watch_depth_ = 0; // only the outermost watch reports

    // Alnum-or-underscore is one class and everything else another, so double-clicking a run of
    // either selects the whole run.
    [[nodiscard]] static bool                         is_word_char(char32_t ch);
    [[nodiscard]] std::pair<std::size_t, std::size_t> word_range(std::size_t index) const;
    // Where Ctrl+Left (back) or Ctrl+Right (forward) goes from `from`; a password's ends.
    [[nodiscard]] std::size_t word_stop(std::size_t from, bool forward) const;

    void reset_caret_blink(); // makes the caret immediately visible and restarts its cycle

    void copy_selection(bool cut); // Ctrl+C/X and the context menu's Cut/Copy
    void paste();                  // Ctrl+V and the context menu's Paste

    // ---- spell checking ----
    // is_word_char() plus apostrophes, so typing "didn't" does not commit "didn" halfway through.
    [[nodiscard]] static bool is_spelling_char(char32_t ch);
    // Every text mutation reports here: drops squiggles the edit touched, shifts the rest, forgets
    // a revertible autocorrection and re-arms the debounced check.
    void                               note_edit(std::size_t pos, std::size_t removed, std::size_t inserted);
    void                               schedule_spell_check(double delay_ms);
    void                               run_spell_check();
    void                               commit_word_before(std::size_t end); // the word ending at `end` is finished
    void                               revert_autocorrection();
    [[nodiscard]] bool                 spelling_shown(const SpellingError& error) const;
    [[nodiscard]] const SpellingError* spelling_error_at(std::size_t index) const;
    void                               forget_spelling(const std::string& word); // after Add/Ignore

    std::string text_;
    std::string placeholder_text_;

    std::optional<std::string> font_family_;
    std::optional<float>       font_size_;
    std::optional<FontWeight>  font_weight_;
    bool                       italic_            = false;
    int                        character_spacing_ = 0;
    Paint                      foreground_;
    Paint                      selection_color_;
    Paint                      unfocused_selection_color_;
    bool                       read_only_      = false;
    bool                       accepts_return_ = false;
    bool                       accepts_tab_    = false;
    bool                       line_numbers_   = false;
    std::vector<TextColorSpan> color_spans_;
    TextWrapping               wrapping_              = TextWrapping::NoWrap;
    std::size_t                max_length_            = 0; // 0: no limit
    CharacterCasing            casing_                = CharacterCasing::Normal;
    bool                       delete_button_enabled_ = true;
    float                      trailing_space_        = 0.0f;
    bool                       delete_hovered_        = false;
    bool                       delete_pressed_        = false;
    std::optional<char32_t>    password_char_;
    std::optional<float>       preferred_x_; // Up/Down keep the column they started from
    std::size_t                caret_            = 0;
    std::size_t                selection_anchor_ = 0;
    ControlHeader              header_{8.0f}; // 8 DIPs between the header and the field
    ControlDescription         description_;

    bool hovered_      = false;
    bool pointer_down_ = false; // left button held: dragging out a selection
    bool chromeless_   = false; // see set_chromeless() above
    bool enter_down_   = false; // mirrors Button's key_down_ Down/Up bookkeeping, Enter only

    // `caret_blink_ms_` wraps instead of resetting on each toggle, so it cannot drift when a tick
    // lands past the threshold.
    bool   caret_visible_  = true;
    double caret_blink_ms_ = 0.0;


    // ---- spell checking state ----
    bool                       spell_check_enabled_ = true;
    SpellChecker*              spell_checker_       = nullptr; // cached by sync_spell_checker()
    std::vector<SpellingError> spelling_errors_;
    bool                       spell_check_pending_  = false;
    double                     spell_check_delay_ms_ = 0.0;   // countdown until the pending full check runs
    std::uint64_t              spell_generation_     = 0;     // checker generation spelling_errors_ reflects
    bool                       typing_word_          = false; // last action typed a word character at the caret

    // The autocorrection Ctrl+Z can still revert, only until the next edit or caret move.
    struct Autocorrection
    {
        std::size_t start = 0;
        std::string original;
        std::string replacement;
    };
    std::optional<Autocorrection> last_autocorrection_;
    std::vector<std::string>      rejected_autocorrections_; // typed words the user reverted, never re-applied

    // Cached by measure(), because handle_pointer() and render() get no TextMeasurer.
    mutable const TextMeasurer* measurer_ = nullptr;
    mutable TextStyle           cached_style_{};
    mutable float               left_inset_  = 0.0f;
    mutable float               line_height_ = 0.0f; // one line of text, for placing squiggles
    mutable Thickness           padding_{};
    mutable float               font_em_             = 14.0f; // the font size, for the delete button's 5em rule
    mutable bool                theme_delete_button_ = true;  // whether the theme draws a DeleteButton at all
    bool                        theme_min_height_    = true;

    // Multi-line layout, kept in step with text_ by sync_layout().
    mutable TextLayout        layout_;
    mutable TextLayoutOptions layout_options_{};
    mutable float             scroll_y_ = 0.0f;
    // Set when the caret or the text moved: the next render scrolls the caret into view, once,
    // so the wheel and the scroll bar can move away from it.
    mutable bool follow_caret_ = false;
    // A multi-line box's vertical scroll bar, and the column an era's permanent bar takes from
    // the text while it shows (0 for the overlay bar).
    ScrollBarOverlay bar_{Orientation::Vertical};
    mutable float    bar_gutter_ = 0.0f;
    // A NoWrap multi-line box scrolls sideways too, with a bar along its bottom.
    mutable float    scroll_x_ = 0.0f;
    ScrollBarOverlay hbar_{Orientation::Horizontal};
    // The line-number column's width (0 without line numbers), set by measure.
    mutable float gutter_width_ = 0.0f;

    Callback<void(const std::string&)>     on_text_changed_;
    Callback<void(const TextBoxEditArgs&)> on_edit_;
    Callback<void()>                       on_submit_;
    Callback<void()>                       on_selection_changed_;

protected:
    void enabled_changed() override;
    /// Called by the Window as focus moves; restarts the caret blink on a gain.
    void focus_changed(bool focused) override;
    /// Caret movement and selection keys, Backspace/Delete, Enter (submit or line break),
    /// Ctrl+A/C/X/V, and Ctrl+Z right after an autocorrection.
    bool key_override(const KeyEvent& e) override;
    /// Inserts typed UTF-8 text at the caret (replacing the selection) and fires
    /// on_text_changed(); control characters are dropped, CharacterCasing and MaxLength apply.
    bool text_override(const TextInputEvent& text_event) override;

protected:
    /// None: the field shows focus with its accent border.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value = this;
    }
    // The whole text (not masked for a password box).
    [[nodiscard]] std::string text_value() const override
    {
        return text_;
    }
    // True while disabled or read-only.
    [[nodiscard]] bool text_value_read_only() const override
    {
        return !is_enabled() || read_only_;
    }
    // Replaces the text as set_text() does, then fires on_text_changed() like a user edit;
    // false (and nothing changes) while disabled or read-only.
    bool set_text_value(std::string_view value) override
    {
        if (!is_enabled() || read_only_)
        {
            return false;
        }
        set_text(std::string(value));
        changed();
        return true;
    }
};

/// An editable text field, single-line by default. Builds a ui::TextBoxControl.
///
/// `.text = prop` is two-way: the box shows the property, and what the user types is
/// written back into it before `.on_text_changed` runs. A bind with a conversion, a `const`
/// property and `ui::bind_one_way()` only display the value. `.accepts_return` or a `.wrapping`
/// other than NoWrap make it multi-line.
///
/// Example:
/// @code
/// ui::Property<std::string> name{"Ada"};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::TextBox{ .header           = "Name",
///                      .placeholder_text = "Your name",
///                      .text             = name,
///                      .max_length       = 40,
///                      .width            = 240,
///                      .on_submit        = [] { save(); } },
///         ui::Text{ .text = ui::bind(name, [](const std::string& n) { return "Hello, " + n; }) },
///     },
/// });
/// @endcode
struct TextBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<TextBox>` points at.
    using control_type = TextBoxControl;

    // ---- properties ----
    /// The label above the control. Empty draws none.
    Binding<std::string> header{};
    /// Secondary text under the box, such as what to enter or why; empty draws none. Screen
    /// readers offer it as help text.
    Binding<std::string> description{};
    /// What is wrong with the value: non-empty shows it in the description's place in the critical
    /// color and draws the border in that color, and screen readers report the field invalid.
    /// The field shows exactly what it is given, so bind it to a check of the value and decide
    /// there when it shows (after the first submit, say).
    Binding<std::string> error{};
    /// The hint shown while the text is empty.
    Binding<std::string> placeholder_text{};
    /// The edited string (UTF-8). `ui::bind(prop)` is two-way: an edit is written back into the
    /// property. Each new bound value replaces the whole text; the text the box already holds
    /// changes nothing, so the write-back keeps the caret.
    TwoWayField<std::string> text{};
    /// The most characters the user can type or paste; 0 is no limit.
    Binding<std::size_t> max_length{0};
    /// Spell checking with squiggles, suggestions and autocorrect. Turn it off for fields that
    /// don't hold prose, such as codes, identifiers and user names.
    Binding<bool> spell_check{true};
    /// Selectable and copyable, never edited.
    Binding<bool> read_only{false};
    /// Enter inserts a line break; the box becomes multi-line.
    Binding<bool> accepts_return{false};
    /// Tab inserts a tab character instead of moving focus (TextBoxControl::set_accepts_tab()).
    Binding<bool> accepts_tab{false};
    /// The casing applied to what the user types or pastes.
    Binding<CharacterCasing> character_casing{CharacterCasing::Normal};
    /// Anything but NoWrap breaks lines at the box's width (multi-line).
    Binding<TextWrapping> wrapping{TextWrapping::NoWrap};
    /// The font family; unset follows the theme.
    Binding<std::optional<std::string>> font_family{};
    /// The font size, DIPs; unset follows the theme.
    Binding<std::optional<float>> font_size{};
    /// The font weight; unset follows the theme.
    Binding<std::optional<FontWeight>> font_weight{};
    /// Italic text.
    Binding<bool> italic{false};
    /// Extra space between characters, in thousandths of an em.
    Binding<int> character_spacing{0};
    /// The text color: a fixed color or a theme resource; unset follows the theme.
    Binding<Paint> foreground{};
    /// The color behind selected text: a fixed color or a theme resource, which follows theme
    /// switches. Unset is the accent.
    Binding<Paint> selection_highlight_color{};
    /// The color behind selected text while the box doesn't have focus; empty (the default) hides
    /// the selection then (TextBoxControl::set_unfocused_selection_highlight_color()).
    Binding<Paint> unfocused_selection_highlight_color{};
    /// Colors for ranges of the text, as a syntax highlighter makes them
    /// (TextBoxControl::set_color_spans()).
    Binding<std::vector<TextColorSpan>> color_spans{};
    /// A column of line numbers left of a multi-line box's text.
    Binding<bool> line_numbers{false};

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Fired after every user edit, with the new text. Handlers in a spec returned from a function must capture by
    /// value or capture state that outlives the window (see spec/Spec.hpp).
    Callback<void(const std::string&)> on_text_changed{};
    /// Every change to the text with what changed, for an incremental parser
    /// (TextBoxControl::on_edit()).
    Callback<void(const TextBoxEditArgs&)> on_edit{};
    /// Fired on Enter (unless `.accepts_return` makes Enter insert a line break).
    Action on_submit{};
    /// The caret or the selection moved (TextBoxControl::on_selection_changed()).
    Action on_selection_changed{};
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
    ControlOut<TextBox> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::TextBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

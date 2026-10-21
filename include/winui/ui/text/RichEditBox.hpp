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

#include <functional>
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

/// The character format one character of a RichEditBox carries. An unset color, family or size
/// follows the control's own (its foreground(), font_family() and font_size()).
struct RichTextFormat
{
    /// Bold weight.
    bool bold = false;
    /// Italic style.
    bool italic = false;
    /// Underlined.
    bool underline = false;
    /// Struck through.
    bool strikethrough = false;
    /// Text color; unset is the control's foreground().
    std::optional<Color> foreground{};
    /// Highlight behind the text; unset paints none.
    std::optional<Color> background{};
    /// Font family; unset is the control's font_family().
    std::optional<std::string> font_family{};
    /// Font size in DIPs; unset is the control's font_size().
    OptionalFloat font_size{};

    friend bool operator==(const RichTextFormat&, const RichTextFormat&) = default;
};

/// A stretch of a RichEditBox's text in one format, as spans() hands out and set_spans() takes.
///
/// Example:
/// @code
/// auto box = std::make_unique<ui::RichEditBoxControl>();
/// box->set_spans({
///     {.text = "Hello, "},
///     {.text = "world", .format = {.bold = true, .foreground = ui::Color::from_argb(0xFFC42B1C)}},
///     {.text = "!\nA second paragraph."},
/// });
/// @endcode
struct RichTextSpan
{
    /// The text (UTF-8); `\n` is a paragraph mark.
    std::string text;
    /// The format every character of `text` carries.
    RichTextFormat format{};

    friend bool operator==(const RichTextSpan&, const RichTextSpan&) = default;
};

/// The fields of a RichTextFormat that can follow the control's own value, as
/// RichEditBoxControl::CharacterFormat::control_default lists them. Flags.
enum class RichTextFormatField : std::uint8_t
{
    None       = 0,                                               ///< no field
    Foreground = 1u << 0,                                         ///< the text color
    Background = 1u << 1,                                         ///< the highlight (none)
    FontFamily = 1u << 2,                                         ///< the font family
    FontSize   = 1u << 3,                                         ///< the font size
    All        = Foreground | Background | FontFamily | FontSize, ///< every one of them
};

/// Combines RichTextFormatField flags.
[[nodiscard]] constexpr RichTextFormatField operator|(RichTextFormatField a, RichTextFormatField b)
{
    return static_cast<RichTextFormatField>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// The flags both `a` and `b` have.
[[nodiscard]] constexpr RichTextFormatField operator&(RichTextFormatField a, RichTextFormatField b)
{
    return static_cast<RichTextFormatField>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/// Whether `set` includes any bit of `flag`.
[[nodiscard]] constexpr bool has_flag(RichTextFormatField set, RichTextFormatField flag)
{
    return (set & flag) != RichTextFormatField::None;
}

/// The Ctrl+B/I/U shortcuts, with their context menu commands, to leave out. Flags.
enum class DisabledFormattingShortcuts : std::uint8_t
{
    None      = 0,                         ///< every shortcut works (the default)
    Bold      = 1u << 0,                   ///< no Ctrl+B / Bold
    Italic    = 1u << 1,                   ///< no Ctrl+I / Italic
    Underline = 1u << 2,                   ///< no Ctrl+U / Underline
    All       = Bold | Italic | Underline, ///< none of them
};

/// Combines DisabledFormattingShortcuts flags.
[[nodiscard]] constexpr DisabledFormattingShortcuts operator|(DisabledFormattingShortcuts a, DisabledFormattingShortcuts b)
{
    return static_cast<DisabledFormattingShortcuts>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// Whether `set` includes any bit of `flag`.
[[nodiscard]] constexpr bool has_flag(DisabledFormattingShortcuts set, DisabledFormattingShortcuts flag)
{
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flag)) != 0;
}

/// An editable, formatted document that looks like a TextBox.
///
/// The document is plain text (`\n` between paragraphs) with a character format per character.
/// The user edits it as in a multi-line TextBox, formats with Ctrl+B/I/U or the context menu,
/// and undoes with Ctrl+Z/Ctrl+Y. The app reads and writes it as text(), spans() or RTF, and
/// formats the selection with set_selection_format(). Only the user's changes raise
/// on_text_changed(), never changes made from code. In a spec tree it is built by
/// `ui::RichEditBox`.
///
/// Example:
/// @code
/// std::string draft; // the saved document, as RTF
/// auto  panel  = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* bold   = panel->add(std::make_unique<ui::ButtonControl>("Bold"));
/// auto* editor = panel->add(std::make_unique<ui::RichEditBoxControl>());
/// editor->set_header("Notes");
/// editor->set_placeholder_text("Type here");
/// editor->set_height(240.0f);
/// editor->load_rtf(R"({\rtf1\ansi Hello, {\b world}!\par})");
/// bold->on_click([editor] { editor->toggle_bold(); });
/// editor->on_text_changed([editor, &draft] { draft = editor->save_rtf(); });
/// window.set_content(std::move(panel));
/// @endcode
class RichEditBoxControl final : public Element, private ValueProvider
{
public:
    /// An empty document.
    RichEditBoxControl();
    /// A document of unformatted `text`, the caret at its end.
    explicit RichEditBoxControl(std::string text);

    /// A character format to apply to a range, or read back from one. Applying it sets every field
    /// that is set and leaves every unset one alone. When reading, a field is unset where the range
    /// mixes values.
    ///
    /// `control_default` lists the color, family and size fields that follow the control's own
    /// value. For the background, that means no highlight. When applied, those fields go back to
    /// the control's value before the set fields are applied. When read, it lists the fields the
    /// whole range leaves to the control.
    ///
    /// Example:
    /// @code
    /// auto editor = std::make_unique<ui::RichEditBoxControl>("Hello, world!");
    /// editor->select(0, 5);
    /// editor->set_selection_format({.italic = true, .font_size = 20.0f}); // bold etc. untouched
    /// if (editor->selection_format().bold == true) { /* ... */ }          // the whole range is bold
    /// editor->set_selection_format({.control_default = ui::RichTextFormatField::Foreground});
    /// @endcode
    struct CharacterFormat
    {
        /// Bold on/off; unset leaves it, or reads as mixed.
        std::optional<bool> bold{};
        /// Italic on/off.
        std::optional<bool> italic{};
        /// Underline on/off.
        std::optional<bool> underline{};
        /// Strikethrough on/off.
        std::optional<bool> strikethrough{};
        /// Text color.
        std::optional<Color> foreground{};
        /// Highlight color.
        std::optional<Color> background{};
        /// Font family.
        std::optional<std::string> font_family{};
        /// Font size in DIPs.
        OptionalFloat font_size{};
        /// The fields that follow the control's own value.
        RichTextFormatField control_default = RichTextFormatField::None;

        friend bool operator==(const CharacterFormat&, const CharacterFormat&) = default;
    };

    // ---- content (the document) ----
    /// The plain text, a `\n` for every paragraph mark.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Replaces the document with unformatted text and puts the caret at its end. Like every
    /// change made from code, it raises no on_text_changed(). It clears the undo history.
    void set_text(std::string text);
    /// The document as runs of one format each, in order.
    [[nodiscard]] std::vector<RichTextSpan> spans() const;
    /// Replaces the document with formatted text; as set_text().
    void set_spans(const std::vector<RichTextSpan>& spans);

    /// Replaces the document with RTF. Text that is not RTF at all comes in as plain text.
    void load_rtf(std::string_view rtf);
    /// The document as RTF, with font and color tables for the formats it uses. load_rtf() reads
    /// it back.
    [[nodiscard]] std::string save_rtf() const;

    // ---- selection ----
    /// The caret's offset into text(): the moving end of the selection.
    [[nodiscard]] std::size_t caret() const
    {
        return caret_;
    }
    /// The selection as [start, end) (the caret when they are equal).
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection() const;
    /// Whether any text is selected (more than a caret).
    [[nodiscard]] bool has_selection() const
    {
        return caret_ != anchor_;
    }
    /// Selects [start, end), the caret at `end`; both are clamped to the text.
    void select(std::size_t start, std::size_t end);
    /// Selects the whole document (Ctrl+A).
    void select_all();
    /// The selected text, plain; empty at a bare caret.
    [[nodiscard]] std::string selected_text() const;

    // ---- character formatting ----
    /// What the whole selection shares, or at a bare caret what will be typed next.
    [[nodiscard]] CharacterFormat selection_format() const;
    /// Applies `format` to the selection as one undo step, or at a bare caret to what is typed
    /// next.
    void set_selection_format(const CharacterFormat& format);
    /// What [start, end) shares.
    [[nodiscard]] CharacterFormat range_format(std::size_t start, std::size_t end) const;
    /// Applies `format` to [start, end), leaving the selection where it is. Not an undo step: it
    /// is for markup the app keeps over the document, such as highlighting search hits.
    void set_range_format(std::size_t start, std::size_t end, const CharacterFormat& format);
    /// Toggles bold: off when the whole selection has it, on otherwise. Ctrl+B.
    void toggle_bold();
    /// As toggle_bold(), for italic. Ctrl+I.
    void toggle_italic();
    /// As toggle_bold(), for underline. Ctrl+U.
    void toggle_underline();

    /// Searches the whole text, case-insensitive: every match as [start, end).
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> find_all(std::string_view text) const;

    /// Replaces the selection as if typed. The maximum length and character casing apply, it is an
    /// undo step and it fires on_text_changed(). Line breaks are kept only while accepts_return()
    /// is on.
    void type_text(std::string_view text);

    // ---- undo ----
    /// Whether there is a step to undo.
    [[nodiscard]] bool can_undo() const
    {
        return !undo_.empty();
    }
    /// Whether there is an undone step to redo.
    [[nodiscard]] bool can_redo() const
    {
        return !redo_.empty();
    }
    /// Restores (Ctrl+Z) the text, formats and selection before the last step.
    void undo();
    /// Reapplies (Ctrl+Y, Ctrl+Shift+Z) the last undone step.
    void redo();
    /// Steps kept before the oldest is dropped.
    static constexpr std::size_t UndoLimit = 100;

    // ---- header and placeholder ----
    /// The label above the box; empty when none is shown.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the box. Empty draws none.
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
    /// The hint shown while the document is empty.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return placeholder_text_;
    }
    /// Drawn in a secondary text color while the document is empty.
    void set_placeholder_text(std::string text)
    {
        placeholder_text_ = std::move(text);
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- the control's own format ----
    /// The font family of text without one of its own; unset follows the theme.
    [[nodiscard]] const std::optional<std::string>& font_family() const
    {
        return font_family_;
    }
    /// Sets or clears the default font family; unset follows the theme. Text without a family of
    /// its own is drawn in it.
    void set_font_family(std::optional<std::string> family)
    {
        font_family_ = std::move(family);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The font size in DIPs of text without one of its own; unset follows the theme.
    [[nodiscard]] std::optional<float> font_size() const
    {
        return font_size_;
    }
    /// Sets or clears the default font size; unset follows the theme.
    void set_font_size(OptionalFloat size)
    {
        font_size_ = size;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The color of text without a color of its own; unset follows the theme.
    [[nodiscard]] const Paint& foreground() const
    {
        return foreground_;
    }
    /// Sets the text color; unset follows the theme. Text without a color of its own is drawn in it.
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
    /// How each line sits between the edges; Left by default.
    [[nodiscard]] TextAlignment text_alignment() const
    {
        return alignment_;
    }
    /// Sets the alignment of every paragraph.
    void set_text_alignment(TextAlignment alignment)
    {
        alignment_ = alignment;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The style text without a format of its own is measured and drawn in.
    [[nodiscard]] TextStyle base_style(const Theme& theme) const;

    /// Whether the text is read-only; false by default.
    [[nodiscard]] bool read_only() const
    {
        return read_only_;
    }
    /// Read-only text can be selected and copied but not edited, and shows no caret. A read-only
    /// box drops its squiggles. Making it editable again checks the spelling again.
    void set_read_only(bool read_only);
    /// Whether Enter starts a new paragraph; true by default.
    [[nodiscard]] bool accepts_return() const
    {
        return accepts_return_;
    }
    /// Enter starts a new paragraph and pasted line breaks are kept. On by default.
    void set_accepts_return(bool accepts)
    {
        accepts_return_ = accepts;
    }
    /// Whether lines break at the box's width; Wrap by default.
    [[nodiscard]] TextWrapping wrapping() const
    {
        return wrapping_;
    }
    /// Sets the wrapping mode.
    void set_wrapping(TextWrapping wrapping)
    {
        wrapping_ = wrapping;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The most characters the user can type or paste; 0 is no limit.
    [[nodiscard]] std::size_t max_length() const
    {
        return max_length_;
    }
    /// The most characters the user can type or paste; 0 (the default) is no limit. Text set
    /// from code is not cut.
    void set_max_length(std::size_t length)
    {
        max_length_ = length;
    }
    /// How typed text is cased; Normal by default.
    [[nodiscard]] CharacterCasing character_casing() const
    {
        return casing_;
    }
    /// Sets the casing of what the user types or pastes from now on.
    void set_character_casing(CharacterCasing casing)
    {
        casing_ = casing;
    }
    /// Which of Ctrl+B/I/U and the matching context menu commands are off; None by default.
    [[nodiscard]] DisabledFormattingShortcuts disabled_formatting_shortcuts() const
    {
        return disabled_shortcuts_;
    }
    /// Turns off formatting shortcuts and their menu commands. Combine flags with `|`, such as
    /// `Bold | Italic`. Takes effect on the next key press or context menu.
    void set_disabled_formatting_shortcuts(DisabledFormattingShortcuts disabled)
    {
        disabled_shortcuts_ = disabled;
    }

    // ---- events ----
    /// Fired after every change the user makes to the text or its formatting, with the text.
    void on_text_changed(Callback<void(const std::string&)> handler)
    {
        on_text_changed_ = std::move(handler);
    }
    /// Fired after the user moves the caret or the selection.
    void on_selection_changed(Callback<void()> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    /// Adds an item of the app's own to the end of the context menu ("Share", say).
    void add_context_menu_item(std::string text, Action on_click);

    // ---- focus ----
    /// Whether the box holds keyboard focus (the caret blinks).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A tab stop while enabled, read-only or not.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }
    // ---- spell checking ----
    /// Whether spell checking is on; true by default. Squiggles and the context menu's suggestions
    /// work as in TextBox. There is no autocorrection.
    [[nodiscard]] bool spell_check() const
    {
        return spell_check_enabled_;
    }
    /// Turns spell checking on or off. Turning it off clears the squiggles, and turning it on
    /// checks the document again.
    void set_spell_check(bool enabled);
    /// The misspellings found in the last check, as offsets into text().
    [[nodiscard]] const std::vector<SpellingError>& spelling_errors() const
    {
        return spelling_errors_;
    }
    /// How long after the last edit the document is checked again, in milliseconds.
    static constexpr double SpellCheckDelayMs = 250.0; // as TextBox
    /// The most suggestions the context menu offers for one misspelling.
    static constexpr std::size_t MaxSpellingSuggestions = 5;
    /// Remembers the host's spell checker and schedules a check.
    void sync_spell_checker(SpellChecker& checker) override;

    // ---- context menu ----
    /// Spelling suggestions for a misspelled word under `position`, then Cut, Copy, Paste,
    /// Bold, Italic, Underline, Undo, Redo and Select All where they apply, then the items from
    /// add_context_menu_item().
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- time ----
    /// How long the caret stays shown, and then hidden, in milliseconds.
    static constexpr double CaretBlinkIntervalMs = 530.0;
    /// Whether a focused box draws its caret right now, in its blink cycle.
    [[nodiscard]] bool caret_visible() const
    {
        return caret_visible_;
    }

    // ---- automation ----
    /// AutomationRole::Edit.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Edit;
    }
    /// The caret, one DIP wide and a line tall, while focused and editable.
    [[nodiscard]] std::optional<Rect> caret_bounds() const override;

protected:
    // ---- layout ----
    /// As TextBox: as wide as the text or the placeholder and as tall as its lines, within
    /// what is offered, plus the header above.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the header and the text area.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Click places the caret (Shift+click extends), drag selects, double-click selects a
    /// word, triple-click a paragraph.
    bool pointer_override(const PointerEvent& e) override;
    /// Scrolls the text vertically; false when there is nothing to scroll, so an enclosing
    /// scroller gets the turn.
    bool wheel_override(const PointerEvent& e) override; // as TextBox
    /// Blinks the caret and runs a pending spell check once its delay has passed.
    void tick_override(Duration delta) override;

    // ---- render ----
    /// The header, the field chrome, the placeholder or the formatted text (scrolled), the
    /// selection, the squiggles and the caret.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // The editing keys and Ctrl shortcuts it handles itself, kept from app shortcuts.
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const;
    // Every arrow key moves the caret; none moves focus.
    [[nodiscard]] bool takes_arrow_key(Key) const
    {
        return true;
    }
    // Enter is a new paragraph while accepts_return() and editable.
    [[nodiscard]] bool takes_enter_key() const
    {
        return accepts_return_ && !read_only_;
    }
    // A run of the document: `length` characters of text_ in one format. The runs cover text_
    // exactly, adjacent runs never share a format, and none is empty.
    struct Run
    {
        std::size_t    length = 0;
        RichTextFormat format{};

        friend bool operator==(const Run&, const Run&) = default;
    };
    struct Snapshot
    {
        std::string      text;
        std::vector<Run> runs;
        std::size_t      anchor = 0;
        std::size_t      caret  = 0;
    };
    enum class EditKind
    {
        Other,
        Typing,
        Deleting,
    };

    [[nodiscard]] bool editable() const
    {
        return is_enabled() && !read_only_;
    }
    [[nodiscard]] bool spelling_on() const
    {
        return spell_check_enabled_ && !read_only_;
    }

    // ---- the document ----
    [[nodiscard]] RichTextFormat format_at(std::size_t index) const;
    // The format typed text takes at the caret: the pending one, else the character before it.
    [[nodiscard]] RichTextFormat typing_format() const;
    void replace(std::size_t start, std::size_t end, std::string_view text, const RichTextFormat& format);
    void insert_runs(std::size_t at, const std::vector<RichTextSpan>& spans);
    void apply_format(std::size_t start, std::size_t end, const CharacterFormat& format);
    void normalize_runs();
    void reset_document(std::string text, std::vector<Run> runs);
    [[nodiscard]] std::vector<RichTextSpan> spans_in(std::size_t start, std::size_t end) const;
    // A loaded format that says what the control already says is stored as "the control's own".
    [[nodiscard]] RichTextFormat unspecified(RichTextFormat format) const;

    // ---- editing ----
    [[nodiscard]] std::string admit(std::string_view text) const; // casing, max length
    void                      begin_edit(EditKind kind);          // records an undo step
    void                      restore(const Snapshot& snapshot);
    [[nodiscard]] Snapshot    snapshot() const;
    void                      user_changed(); // marks dirty, re-arms spelling, raises on_text_changed
    void                      erase_selection();
    void                      delete_backward(bool word);
    void                      delete_forward(bool word);
    void                      toggle(std::optional<bool> CharacterFormat::* field);
    void                      copy_selection(bool cut);
    void                      paste();
    void                      move_caret(std::size_t to, bool extend);
    void                      move_vertically(int lines, bool extend);
    [[nodiscard]] std::size_t word_left(std::size_t from) const;
    [[nodiscard]] std::size_t word_right(std::size_t from) const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> word_range(std::size_t index) const;
    void                                              reset_caret_blink();
    void                                              notify_selection(std::pair<std::size_t, std::size_t> before);

    // ---- layout ----
    [[nodiscard]] Rect                             text_area() const;
    [[nodiscard]] Rect                             bar_viewport() const; // the scroll bar's column, over the right edge
    void                                           sync_layout() const;
    [[nodiscard]] std::size_t                      hit_at(Point window_point) const;
    [[nodiscard]] std::vector<TextLayoutParagraph> layout_content() const;

    // ---- spelling ----
    void                               note_edit(std::size_t pos, std::size_t removed, std::size_t inserted);
    void                               run_spell_check();
    [[nodiscard]] const SpellingError* spelling_error_at(std::size_t index) const;

    std::string                   text_;
    std::vector<Run>              runs_;
    std::optional<RichTextFormat> pending_format_; // set at a bare caret, for what is typed next
    std::size_t                   caret_  = 0;
    std::size_t                   anchor_ = 0;
    std::optional<float>          preferred_x_;

    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    EditKind              last_edit_ = EditKind::Other;

    ControlHeader               header_{8.0f};
    ControlDescription          description_;
    std::string                 placeholder_text_;
    std::optional<std::string>  font_family_;
    std::optional<float>        font_size_;
    Paint                       foreground_;
    Paint                       selection_color_;
    TextAlignment               alignment_          = TextAlignment::Left;
    TextWrapping                wrapping_           = TextWrapping::Wrap;
    bool                        accepts_return_     = true;
    bool                        read_only_          = false;
    std::size_t                 max_length_         = 0;
    CharacterCasing             casing_             = CharacterCasing::Normal;
    DisabledFormattingShortcuts disabled_shortcuts_ = DisabledFormattingShortcuts::None;

    bool   hovered_        = false;
    bool   pointer_down_   = false;
    bool   caret_visible_  = true;
    double caret_blink_ms_ = 0.0;


    bool                       spell_check_enabled_ = true;
    SpellChecker*              spell_checker_       = nullptr;
    std::vector<SpellingError> spelling_errors_;
    bool                       spell_check_pending_  = false;
    double                     spell_check_delay_ms_ = 0.0;
    std::uint64_t              spell_generation_     = 0;

    struct ExtraMenuItem
    {
        std::string text;
        Action      on_click;
    };
    std::vector<ExtraMenuItem> extra_menu_items_;

    Callback<void(const std::string&)> on_text_changed_;
    Callback<void()>                   on_selection_changed_;

    // Cached by measure() for input and render, which get no measurer (as TextBox).
    mutable const TextMeasurer* measurer_ = nullptr;
    mutable TextStyle           base_style_{};
    mutable Thickness           padding_{};
    mutable TextLayout          layout_;
    mutable TextLayoutOptions   layout_options_{};
    mutable float               scroll_y_       = 0.0f;
    mutable std::size_t         revealed_caret_ = static_cast<std::size_t>(-1); // the caret the view last followed
    ScrollBarOverlay            bar_{Orientation::Vertical}; // shows while the text is taller than the box

protected:
    // ---- behavior ----
    void enabled_changed() override;
    /// Called by Window as focus moves: restarts the caret blink.
    void focus_changed(bool focused) override;
    /// Caret movement, deletion, clipboard, undo/redo and the formatting shortcuts.
    bool key_override(const KeyEvent& e) override;
    /// Typed text replaces the selection in the typing format, with the max length and character
    /// casing applied. Consecutive typing is one undo step.
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
    // text().
    [[nodiscard]] std::string text_value() const override
    {
        return text_;
    }
    // True while disabled or read-only.
    [[nodiscard]] bool text_value_read_only() const override
    {
        return !editable();
    }
    // Replaces the whole text as if typed (an undo step, raising
    // on_text_changed()); refused while not editable.
    bool set_text_value(std::string_view value) override;
};

/// Editable rich text: character formatting, undo, RTF in and out.
/// Builds a ui::RichEditBoxControl.
///
/// Formatting is applied through the built control (via `.control`); the spec only sets up the
/// document and its editing rules.
///
/// Example:
/// @code
/// ui::Control<ui::RichEditBox> editor;
/// bool                dirty  = false;
///
/// window.set_content(ui::RichEditBox{
///     .header           = "Notes",
///     .placeholder_text = "Write here",
///     .height           = 200,
///     .on_text_changed  = [&] { dirty = true; },
///     .control          = editor,
/// });
/// @endcode
struct RichEditBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<RichEditBox>` points at.
    using control_type = RichEditBoxControl;

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
    /// The hint shown while the document is empty.
    Binding<std::string> placeholder_text{};
    /// The document as plain text; a new value replaces it (and its formatting), one equal to
    /// what the box holds changes nothing. `ui::bind(prop)` is two-way: what the user types is
    /// written back as plain text, before `.on_text_changed` runs.
    TwoWayField<std::string> text{};
    /// The most characters the user can type or paste; 0 is no limit.
    Binding<std::size_t> max_length{0};
    /// Whether misspelled words are underlined, with suggestions in the context menu.
    Binding<bool> spell_check{true};
    /// Selectable and copyable, never edited.
    Binding<bool> read_only{false};
    /// Enter starts a new paragraph. On by default.
    Binding<bool> accepts_return{true};
    /// Sets the casing of what the user types or pastes.
    Binding<CharacterCasing> character_casing{CharacterCasing::Normal};
    /// The Ctrl+B/I/U shortcuts to leave out.
    Binding<DisabledFormattingShortcuts> disabled_formatting_shortcuts{DisabledFormattingShortcuts::None};
    /// Whether lines break at the box's width; Wrap by default.
    Binding<TextWrapping> wrapping{TextWrapping::Wrap};
    /// The alignment of every paragraph.
    Binding<TextAlignment> text_alignment{TextAlignment::Left};
    /// The default font family; unset follows the theme.
    Binding<std::optional<std::string>> font_family{};
    /// The default font size, DIPs; unset follows the theme.
    Binding<std::optional<float>> font_size{};
    /// The color of text without its own: a fixed color or a theme resource; unset follows the
    /// theme.
    Binding<Paint> foreground{};
    /// The color behind selected text: a fixed color or a theme resource, which follows theme
    /// switches. Unset is the accent.
    Binding<Paint> selection_highlight_color{};

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
    /// Fired after every change the user makes to the text or its formatting. Handlers in a
    /// spec returned from a function must capture by value or capture state that outlives the
    /// window (see spec/Spec.hpp).
    Callback<void(const std::string&)> on_text_changed{};
    /// The caret moved or the selection grew or shrank.
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
    /// Out: a non-owning observer of the built control, used to format text or read RTF.
    ControlOut<RichEditBox> control{};

    /// Consumes the spec and returns the built ui::RichEditBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

// SPDX-License-Identifier: MIT
#pragma once
#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/ui/hosting/NativeControl.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstdint>

namespace native_controls
{

/// A real RICHEDIT50W child window in the layout tree, with the document calls the gallery
/// samples use: character formatting, find, RTF in and out, a text-changed handler, spell checking and a
/// context menu with app items.
///
/// A NativeControl: its HWND is created by the window's NativeHost when it is first laid out
/// under ui::run(), and follows the window's light/dark mode only. Offsets are UTF-8 byte
/// offsets in which each paragraph mark is one '\r'. Has no ui:: spec; build it imperatively and
/// drop the unique_ptr into a declared tree. The HWND-free editor with the same document calls
/// is ui::RichEditBoxControl.
///
/// Example:
/// @code
/// auto editor = std::make_unique<native_controls::RichEditBox>(ui::Size{400.0f, 200.0f}, "Hello, world");
/// editor->set_accessibility({.name = "Notes"});
/// editor->add_context_menu_item("Share", [&] { share(); });
/// native_controls::RichEditBox* notes = editor.get();
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Button{.text = "Bold", .on_click = [notes] { notes->toggle_bold(); notes->focus(); }},
///         std::move(editor),
///     },
/// });
///
/// // Later: mark every "world" red, and keep the document as RTF.
/// for (const auto& [start, end] : notes->find_all("world"))
/// {
///     notes->set_range_format(start, end, {.foreground = ui::Color::from_argb(0xFFC42B1C)});
/// }
/// const std::string rtf = notes->save_rtf();
/// @endcode
class RichEditBox final : public ui::NativeControl
{
public:
    /// A box of `size` DIPs holding `initial_text` (UTF-8). Loads MSFTEDIT.DLL on first use.
    explicit RichEditBox(ui::Size size, std::string initial_text = "");

    /// Replaces the whole text with plain `text` (UTF-8), dropping all formatting. A no-op until
    /// the window exists; pass the first text to the constructor instead.
    void set_text(const std::string& text);
    /// The plain text, UTF-8; empty until the window exists. Paragraph marks come back as a CRLF
    /// pair here, unlike in the offsets.
    [[nodiscard]] std::string text() const;

    // ---- rich text ----
    /// Character formatting — ITextCharacterFormat's properties, each unset field left alone. A
    /// fully transparent color means the control's own (its window text and background).
    ///
    /// Example:
    /// @code
    /// editor->set_selection_format({.bold = true, .foreground = ui::colors::Black});
    /// @endcode
    struct CharacterFormat
    {
        std::optional<bool>      bold{};       ///< ITextCharacterFormat.Bold.
        std::optional<bool>      italic{};     ///< ITextCharacterFormat.Italic.
        std::optional<bool>      underline{};  ///< A single underline or none.
        std::optional<ui::Color> foreground{}; ///< Text color; alpha is ignored except fully transparent.
        std::optional<ui::Color> background{}; ///< Highlight color behind the text; same alpha rule.
    };

    /// The selection as [start, end) (the caret when they are equal).
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection() const;
    /// Selects [start, end) (a caret when equal). A no-op until the window exists.
    void select(std::size_t start, std::size_t end);
    /// The formatting the whole selection shares: a field is unset where the selection mixes.
    [[nodiscard]] CharacterFormat selection_format() const;
    /// Applies `format` to the selection, or at the caret to what is typed next.
    void set_selection_format(const CharacterFormat& format);
    /// Applies `format` to [start, end), leaving the selection where it is.
    void set_range_format(std::size_t start, std::size_t end, const CharacterFormat& format);
    /// Toggles bold: off when the whole selection has it, on otherwise.
    void toggle_bold();
    /// Toggles italic; see toggle_bold().
    void toggle_italic();
    /// ITextRange::FindText over the whole text, case-insensitive: every match as [start, end).
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> find_all(std::string_view text) const;

    /// Replaces the content with RTF.
    void load_rtf(std::string_view rtf);
    /// The whole content as RTF, which
    /// load_rtf() reads back.
    [[nodiscard]] std::string save_rtf() const;

    /// Gives the control keyboard focus.
    void focus();

    /// Called after every change to the text, whoever made it. One handler; a new one
    /// replaces the old.
    void on_text_changed(ui::Callback<void()> handler)
    {
        on_text_changed_ = std::move(handler);
    }

    /// Adds an item of the app's own to the end of the context menu, the way the gallery adds
    /// "Share". A std::function, as other menu handlers.
    void add_context_menu_item(std::string text, std::function<void()> on_click);

    // ---- spell checking ----
    /// Turns spell checking on or off; on by default, mirroring TextBoxControl::set_spell_check_enabled().
    ///
    /// The framework does the checking, not the control. RichEdit 8's own IMF_SPELLCHECKING is
    /// inert for a plain HWND host — measured, so nobody retries it —
    /// so the squiggle is drawn as ordinary character formatting instead: the window's
    /// SpellChecker (the same one every TextBox uses, so the same dictionaries and the same
    /// "Add to dictionary") reports the misspelled ranges, and each gets CFU_UNDERLINEWAVE
    /// applied over it. Suggestions then come out of that same error list, in the context menu
    /// above.
    ///
    /// Three consequences worth knowing:
    ///  - The wave is **red**, and getting there costs one `EM_STREAMIN` per misspelling.
    ///    `EM_SETCHARFORMAT` can draw the wave but cannot color it — RichEdit 8's
    ///    `CHARFORMAT2W::bUnderlineColor` is accepted, discarded and reads back 0, and there is
    ///    no `CFM_` mask or TOM property for it either — so each marked word is streamed back
    ///    over itself as `{\rtf1\ansi{\colortbl ;\red255\green0\blue0;}\ulwave\ulc1 <word>}`
    ///    with `SFF_SELECTION`. That replaces the word with itself, which is why the escaping in
    ///    `escape_rtf()` has to be exact; in exchange the text stays byte-identical, the
    ///    viewport does not scroll, the word keeps its own font/size/color, and the marks stay
    ///    off the undo stack. The color is **pure 255,0,0 and nothing else**: RichEdit snaps
    ///    `\ulc` onto a fixed 16-color palette and drops anything off it, so this red cannot be
    ///    themed and does not match the one `TextBox` strokes its own squiggle with. All of it
    ///    measured on RichEdit 8.
    ///  - Applying the marks clears CFU_UNDERLINE* across the whole document first. Harmless
    ///    here precisely because this control exposes no formatting API — nothing else can
    ///    have set an underline — but it is the reason adding one later is not free.
    ///  - Checking runs on the UI thread, on every EN_CHANGE, with no debounce: TextBox waits
    ///    out a typing pause, this does not. Fine for the paragraph-sized text a native escape
    ///    hatch holds; a long document would want the same delay.
    void set_spell_check_enabled(bool enabled);
    /// Whether spell checking is on (see set_spell_check_enabled()).
    [[nodiscard]] bool spell_check_enabled() const
    {
        return spell_check_enabled_;
    }

    /// Element's own seam, reached here through NativeHost::spell_checker() on every
    /// sync_native() rather than through focus, since real focus lives in the child HWND
    /// (render/NativeHost.hpp). Cheap: re-checks only when something actually moved.
    void sync_spell_checker(ui::SpellChecker& checker) override;

    /// The RichEdit told its parent its text changed (EN_CHANGE), which is the only way an
    /// edit made straight into the child HWND can reach the framework at all.
    void on_native_content_changed() override;

    // ---- context menu ----
    /// RICHEDIT50W, unlike a plain EDIT, has no context menu of its own: a rich edit control
    /// asks its *host* for one (IRichEditOleCallback::GetContextMenu) and, with no callback
    /// set, simply forwards WM_CONTEXTMENU to its parent and shows nothing. So the standard
    /// Undo/Cut/Copy/Paste/Delete/Select All menu every Windows text field has is written out
    /// here, through the framework's own ContextMenuHost — the same seam TextBox uses, so the
    /// menu picks up the window's light/dark palette (Win32ContextMenuHost.cpp) rather than
    /// being a second, differently-themed popup. Reached via
    /// Win32NativeHost::owner_for() → Window::handle_native_context_menu(); every item is
    /// applied by sending the control the ordinary edit message for it, so the RichEdit's own
    /// undo stack, clipboard formats and read-only state stay authoritative.
    ///
    /// Its spelling section — suggestions, Add to dictionary, Ignore — is built from the same
    /// error list the squiggles are drawn from, and appears only when the click landed on one
    /// of them. Same wording and order as TextBox's, so the two menus read alike.
    bool handle_context_menu(std::optional<ui::Point> position, ui::ContextMenuHost& host) override;

protected:
    /// Sends the two one-time messages the spell checking above depends on.
    void on_native_created(ui::NativeHandle handle) override;

private:
    // WM_GETTEXT returns CRLF per paragraph mark, but EM_EXSETSEL counts it as one character,
    // so offsets need this text, not text().
    [[nodiscard]] std::string              raw_text(void* handle) const;
    void                                   run_spell_check();
    void                                   apply_spelling_marks(void* handle) const;
    [[nodiscard]] const ui::SpellingError* spelling_error_at(std::size_t index) const;

    static constexpr std::size_t MaxSpellingSuggestions = 5; // as TextBox

    ui::SpellChecker*              spell_checker_ = nullptr; // non-owning; published by the host
    std::vector<ui::SpellingError> spelling_errors_;
    std::string                    checked_text_;            // what spelling_errors_ was computed over
    std::uint64_t                  spell_generation_    = 0; // SpellChecker::generation() at that point
    bool                           spell_check_enabled_ = true;
    bool                           spell_dirty_         = true;

    ui::Callback<void()> on_text_changed_;
    struct ExtraMenuItem
    {
        std::string           text;
        std::function<void()> on_click;
    };
    std::vector<ExtraMenuItem> extra_menu_items_;
};

} // namespace native_controls

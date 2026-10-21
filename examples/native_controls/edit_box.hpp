// SPDX-License-Identifier: MIT
#pragma once
#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/hosting/NativeControl.hpp"

#include <optional>
#include <string>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace native_controls
{

/// The EDIT styles the class reads only when the window is created.
///
/// Example:
/// @code
/// native_controls::EditBoxOptions secret;
/// secret.password = true; // also forces one line
/// auto box = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", secret);
/// @endcode
struct EditBoxOptions
{
    /// Several lines (ES_MULTILINE | ES_WANTRETURN, with a vertical scroll bar),
    /// Enter inserting a line break. Off: one line that scrolls sideways under the caret.
    bool accepts_return = true;
    /// The wrapping of a multi-line box: anything but NoWrap breaks lines at the box's width;
    /// NoWrap scrolls sideways instead, with a horizontal scroll bar. One line never wraps.
    ui::TextWrapping wrapping = ui::TextWrapping::Wrap;
    /// The text alignment (ES_LEFT/ES_CENTER/ES_RIGHT); Justify is Left, which the class has no
    /// other answer for.
    ui::TextAlignment alignment = ui::TextAlignment::Left;
    /// A password box (ES_PASSWORD): every character drawn as password_char(), no Cut or Copy.
    /// The class allows it on one line only, so it turns accepts_return off.
    bool password = false;
};

/// A stock Win32 EDIT child window in the layout tree: the plainest native control, with the
/// TextBox and PasswordBox features the class can do.
///
/// A NativeControl, so its HWND is created by the window's NativeHost the first time it is laid
/// out under ui::run(); every setter works before that and is applied on creation. Has no
/// ui:: spec: build it imperatively and drop the unique_ptr into a declared tree (ui::Spec and
/// ui::Spec accept one). No spell checking and no per-range formatting — use native_controls::RichEditBox
/// for those. Follows the window's light/dark mode, not the rest of its Theme.
///
/// Example:
/// @code
/// native_controls::EditBoxOptions single;
/// single.accepts_return = false;
///
/// auto name = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", single);
/// name->set_header("Enter your name:");
/// name->set_placeholder_text("Name");
/// name->set_max_length(40);
/// native_controls::EditBox* observer = name.get();
/// name->on_text_changed([&, observer] { greeting = "Hello, " + observer->text(); }); // Property<std::string>
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {std::move(name), ui::Text{.text = greeting}},
/// });
/// @endcode
class EditBox final : public ui::NativeControl
{
public:
    /// `size` is the box itself, the header excluded; a height of 0 is one line of the box's
    /// font (the height an EDIT in a dialog gets), re-read whenever the font changes.
    explicit EditBox(ui::Size size, std::string initial_text = "", EditBoxOptions options = {});
    /// Frees the font and brush it made; ~NativeControl then destroys the HWND.
    ~EditBox() override;

    /// The creation-time styles, as passed to the constructor (password forces accepts_return
    /// off).
    [[nodiscard]] const EditBoxOptions& options() const
    {
        return options_;
    }

    /// Replaces the whole text (WM_SETTEXT), UTF-8, calling the on_text_changed() handler. Called before the
    /// window exists, it just becomes the initial text.
    void set_text(const std::string& text);
    /// The current text, UTF-8, read from the window (or the pending text before it exists).
    [[nodiscard]] std::string text() const;

    // ---- text box properties ----
    /// The label above the box, as ControlHeader draws it for TextBox (8 DIPs above the box).
    /// The box's bounds() are below it.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// Sets the header; an empty one takes no space. Relayouts.
    void set_header(std::string header);

    /// The placeholder (EM_SETCUEBANNER): shown while the box is empty, focused or not. The
    /// class draws it on a single-line box only.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return placeholder_;
    }
    /// Sets the placeholder; needs comctl32 6 (an app manifest) to show.
    void set_placeholder_text(std::string text);

    /// Read-only (EM_SETREADONLY): selectable and copyable, never edited. The class draws a
    /// read-only box on the dialog color, as every Windows read-only field is.
    [[nodiscard]] bool read_only() const
    {
        return read_only_;
    }
    /// Makes the box read-only or editable again.
    void set_read_only(bool read_only);

    /// Enabled (EnableWindow): a disabled box grays its text and its header.
    [[nodiscard]] bool enabled() const
    {
        return enabled_;
    }
    /// Enables or disables the box (EnableWindow); the header grays with it.
    void set_enabled(bool enabled);

    /// The length limit (EM_SETLIMITTEXT): the most characters the user can type or paste; 0 is no
    /// limit. Text already in the box, or set from code, is not cut.
    [[nodiscard]] int max_length() const
    {
        return max_length_;
    }
    /// Sets the typing limit; 0 removes it.
    void set_max_length(int length);

    /// The character casing (ES_LOWERCASE/ES_UPPERCASE): what the user types from now on.
    [[nodiscard]] ui::CharacterCasing character_casing() const
    {
        return casing_;
    }
    /// Sets the casing of future typing; existing text is not converted.
    void set_character_casing(ui::CharacterCasing casing);

    /// ES_NUMBER: only digits can be typed (a paste is not checked, as the class does not).
    [[nodiscard]] bool number_only() const
    {
        return number_only_;
    }
    /// Turns digit-only typing on or off.
    void set_number_only(bool number_only);

    // ---- password box properties ----
    /// The mask character (EM_SETPASSWORDCHAR), U+25CF by default. A password box only.
    [[nodiscard]] char32_t password_char() const
    {
        return password_char_;
    }
    /// Sets the mask character; ignored on a box created without EditBoxOptions::password.
    void set_password_char(char32_t mask);
    /// Whether the password shows as typed (true) or masked (false). A password box only.
    [[nodiscard]] bool password_revealed() const
    {
        return password_revealed_;
    }
    /// Shows the password as typed (true) or masked again (false), e.g. from a "Show" CheckBox.
    void set_password_revealed(bool revealed);

    // ---- font and colors ----
    /// The font family; empty is the system message font's (Segoe UI).
    void set_font_family(std::string family);
    /// The font size in DIPs; 0 is the system message font's.
    void set_font_size(float size);
    /// The font weight; unset is the system message font's.
    void set_font_weight(std::optional<ui::FontWeight> weight);
    /// Italic or upright text.
    void set_italic(bool italic);
    /// The text color; unset is the system's window text, or the palette's text in a dark theme.
    void set_foreground(std::optional<ui::Color> color);
    /// The fill; unset is the system's, or an opaque dark fill in a dark theme. Opaque only:
    /// the class paints with a solid brush.
    void set_background(std::optional<ui::Color> color);

    // ---- selection ----
    /// The selection as [start, end) (the caret when they are equal).
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection() const;
    /// Selects [start, end), scrolled into view.
    void select(std::size_t start, std::size_t end);
    /// Selects the whole text.
    void select_all();
    /// Gives the box keyboard focus.
    void focus();

    /// Called after every change to the text, typed, pasted or set from code. Read the new
    /// text with text(). One handler; a new one replaces the old.
    void on_text_changed(ui::Callback<void()> handler)
    {
        on_text_changed_ = std::move(handler);
    }
    /// Re-creates the font when the window's DPI changed since it was made.
    void sync_native(ui::NativeHost& host) override;
    /// Answers WM_CTLCOLOREDIT/WM_CTLCOLORSTATIC with the box's colors.
    bool handle_native_notification(ui::NativeNotification& notification) override;
    /// EN_CHANGE: calls the on_text_changed() handler.
    void on_native_content_changed() override;

    /// The standard Undo/Cut/Copy/Paste/Delete/Select All menu, built through the framework's
    /// own ContextMenuHost so it picks up the window's palette. Same wording and order as
    /// RichEditBox's, minus Redo and its spelling section — a plain EDIT keeps a one-level undo
    /// and has no redo to offer, and see the file header for the spelling half. A password box
    /// offers neither Cut nor Copy.
    bool handle_context_menu(std::optional<ui::Point> position, ui::ContextMenuHost& host) override;

protected:
    /// Applies everything set so far, and takes over the context menu; see the .cpp.
    void on_native_created(ui::NativeHandle handle) override;

    // ---- NativeControl / Element ----
    /// The requested size plus the header; a zero height is one line of the current font.
    [[nodiscard]] ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override;
    /// Places the header on top and the EDIT window below it.
    void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext& context) override;
    /// Draws the header, and follows the theme's light/dark mode into the window.
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override;


private:
    void apply_font(void* handle);
    void apply_style_bit(std::uint32_t bit, bool on) const;
    void apply_theme(void* handle) const;

    EditBoxOptions      options_;
    float               requested_height_ = 0.0f; // 0: one line of the font
    std::string         pending_text_;            // the text until the window exists
    ui::ControlHeader   header_{8.0f};            // 8 DIPs between header and box
    std::string         placeholder_;
    bool                read_only_         = false;
    bool                enabled_           = true;
    int                 max_length_        = 0;
    ui::CharacterCasing casing_            = ui::CharacterCasing::Normal;
    bool                number_only_       = false;
    char32_t            password_char_     = U'\x25CF';
    bool                password_revealed_ = false;
    bool                text_pending_      = false; // set_text() before the window existed

    std::string                   font_family_;
    float                         font_size_ = 0.0f;
    std::optional<ui::FontWeight> font_weight_;
    bool                          italic_ = false;
    std::optional<ui::Color>      foreground_;
    std::optional<ui::Color>      background_;

    void*    font_     = nullptr; // HFONT, owned
    unsigned font_dpi_ = 0;       // the DPI font_ was made for

    // The theme's light/dark mode as render_override() last saw it, and the colors it implies.
    mutable bool      dark_       = false;
    mutable bool      theme_seen_ = false;
    mutable ui::Color theme_text_{};
    mutable ui::Color theme_fill_{};
    // The one brush WM_CTLCOLOR* hands out, owned, re-made when the color changes.
    mutable void*     brush_ = nullptr;
    mutable ui::Color brush_color_{};

    ui::Callback<void()> on_text_changed_;
};

} // namespace native_controls

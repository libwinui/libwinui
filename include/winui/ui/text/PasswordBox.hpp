// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cstddef>

namespace ui
{

/// How a ui::PasswordBox shows its password.
enum class PasswordBoxRevealMode
{
    /// Masked, with a reveal button that shows the password while held down (the default). The
    /// button appears only while the box is focused, for a password typed since the box was last
    /// empty.
    Peek,
    Hidden,  ///< Always masked; no reveal button.
    Visible, ///< Always shown in clear text; no reveal button.
};

/// A single-line field that masks what is typed into it.
///
/// It owns a `TextBox` as its edit part, masked with `password_char()`. Caret, selection,
/// clipboard and context menu work on the hidden text. A password offers no Cut or Copy, is never
/// spell checked, and screen readers get only mask characters. In the default
/// `PasswordBoxRevealMode::Peek`, a reveal button shows the password while it is held. The button
/// shows only for a password typed since the field was last empty, so whoever sits down next
/// can't peek at a password filled in earlier. In a spec tree it is built by `ui::PasswordBox`.
///
/// Example:
/// @code
/// ui::Control<ui::PasswordBox> box;
/// window.set_content(ui::PasswordBox{
///     .header           = "Password",
///     .placeholder_text = "Enter your password",
///     .max_length       = 64,
///     .width            = 300.0f,
///     .on_password_changed = [&box](const std::string& password) {
///         if (password == "Password")
///             box->set_password(""); // rejecting a value from inside the handler is fine
///     },
///     .control = box,
/// });
///
/// // Imperatively:
/// auto pin = std::make_unique<ui::PasswordBoxControl>();
/// pin->set_password_char(U'*');
/// pin->set_reveal_mode(ui::PasswordBoxRevealMode::Hidden);
/// pin->on_password_changed([](const std::string& p) { /* validate p */ });
/// @endcode
class PasswordBoxControl final : public Element, private ValueProvider
{
public:
    /// An empty, enabled box in Peek mode, masking with U+25CF.
    PasswordBoxControl();
    ~PasswordBoxControl() override;

    // ---- content ----
    /// The password in clear text (UTF-8), whatever the reveal mode.
    [[nodiscard]] const std::string& password() const
    {
        return box_->text();
    }
    /// Replaces the password. Does not fire on_password_changed(). An empty password hides the
    /// reveal button until something is typed again.
    void set_password(std::string password);

    /// The code point drawn once per hidden character; U+25CF (a black circle) by default.
    [[nodiscard]] char32_t password_char() const
    {
        return password_char_;
    }
    /// Sets the mask character. The drawn mask and what screen readers get change at once.
    void set_password_char(char32_t mask);

    /// When the password can be revealed; Peek by default.
    [[nodiscard]] PasswordBoxRevealMode reveal_mode() const
    {
        return reveal_mode_;
    }
    /// Sets when the password can be revealed. Switching to Visible shows the text at once, and
    /// switching away masks it again.
    void set_reveal_mode(PasswordBoxRevealMode mode);

    /// Whether the password shows right now: in Visible mode, or while the reveal button is held.
    [[nodiscard]] bool password_revealed() const
    {
        return revealed_;
    }
    /// Whether the reveal button is shown.
    [[nodiscard]] bool reveal_button_visible() const;
    /// The reveal button's rectangle, a 30 DIP column at the field's right edge, in the same
    /// coordinates as `bounds()`. Meaningful only while `reveal_button_visible()`.
    [[nodiscard]] Rect reveal_button_rect() const;

    /// The label drawn above the field; empty (the default) draws none. Screen readers also use
    /// it as the name when none is set.
    [[nodiscard]] const std::string& header() const
    {
        return box_->header();
    }
    /// Sets the header.
    void set_header(std::string header)
    {
        box_->set_header(std::move(header));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The secondary text under the field; empty (the default) draws none. Screen readers also
    /// offer it as help text when no description is set.
    [[nodiscard]] const std::string& description() const
    {
        return box_->description();
    }
    /// Sets the description. Empty draws none and takes no room.
    void set_description(std::string description)
    {
        box_->set_description(std::move(description));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// What is wrong with the value, shown in the description's place in the critical color,
    /// with the border in the same color; empty (the default) shows the description.
    [[nodiscard]] const std::string& error() const
    {
        return box_->error();
    }
    /// Sets the error. Empty shows the description again.
    void set_error(std::string error)
    {
        box_->set_error(std::move(error));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The description, or the error while one is set, when accessibility().description is empty.
    [[nodiscard]] std::string automation_help_text_fallback() const override
    {
        return box_->automation_help_text_fallback();
    }
    /// Invalid while an error is set.
    [[nodiscard]] bool automation_invalid() const override
    {
        return box_->automation_invalid();
    }
    /// The hint shown while the password is empty.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return box_->placeholder_text();
    }
    /// Sets the hint shown while the password is empty.
    void set_placeholder_text(std::string text)
    {
        box_->set_placeholder_text(std::move(text));
        mark_dirty(DirtyFlags::Visual);
    }
    /// The character limit set by `set_max_length()`; 0 is no limit.
    [[nodiscard]] std::size_t max_length() const
    {
        return box_->max_length();
    }
    /// The most characters the user can type or paste; 0 (the default) is no limit.
    void set_max_length(std::size_t length)
    {
        box_->set_max_length(length);
    }

    /// Whether the box accepts input and focus; true by default.
    [[nodiscard]] bool enabled() const
    {
        return box_->is_enabled();
    }

    /// Fired after the user changes the password, with the new one.
    void on_password_changed(Callback<void(const std::string&)> handler)
    {
        on_password_changed_ = std::move(handler);
    }
    /// Fired when the user presses Enter in the box. Without a handler Enter is left to the
    /// window, which clicks its default button.
    void on_submit(Callback<void()> handler)
    {
        box_->on_submit(std::move(handler));
    }

    /// Focusable while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return box_->is_enabled();
    }
    /// Whether the box currently has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

protected:
    /// Adds the inner text box, which for_each_child() leaves out.
    void build_visual_tree_override(VisualTree& tree) const override;

public:
    // ---- automation ----
    /// The text field's cursor over the text field: an I-beam, or an arrow over its buttons.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override
    {
        return box_->bounds().contains(p) ? box_->cursor_at(p) : std::nullopt;
    }
    /// Always AutomationRole::Edit.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Edit;
    }
    /// The header, when no `Accessibility::name` is set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return box_->header();
    }
    /// The inner text box's caret.
    [[nodiscard]] std::optional<Rect> caret_bounds() const override
    {
        return box_->caret_bounds();
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    bool               pointer_override(const PointerEvent& e) override;
    void               hit_test_children(Point p, std::vector<Element*>& out) const override;
    [[nodiscard]] bool covers_children_at(Point p) const override;

    void clear_dirty_override(DirtyFlags flags) override;
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const
    {
        return box_->takes_shortcut(e);
    }
    [[nodiscard]] bool takes_enter_key() const
    {
        return box_->takes_enter_key();
    }
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return box_->takes_arrow_key(key);
    }
    void apply_reveal();
    void edited(const std::string& text);

    std::unique_ptr<TextBoxControl> box_;
    char32_t                        password_char_ = U'\x25CF';
    PasswordBoxRevealMode           reveal_mode_   = PasswordBoxRevealMode::Peek;
    bool                            revealed_      = false;
    // Whether the reveal button may show: set when the password goes from empty to typed, cleared on focus.
    bool can_show_reveal_ = false;
    bool was_empty_       = true;
    bool reveal_hovered_  = false;
    bool reveal_pressed_  = false;

    mutable float font_em_ = 14.0f;

    Callback<void(const std::string&)> on_password_changed_;

protected:
    void enabled_changed() override;
    /// Gaining focus hides the reveal button until the field is cleared and typed into again.
    void focus_changed(bool focused) override;
    /// Forwarded to the inner TextBox while focused. Enter is left to the window unless there is
    /// a submit handler.
    bool key_override(const KeyEvent& e) override;
    /// Forwarded to the inner TextBox while focused. Each edit fires `on_password_changed()`.
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
    // One `password_char()` per code point of the password.
    [[nodiscard]] std::string text_value() const override
    {
        const std::string one = encode_utf8(password_char_);
        std::string       out;
        for (std::size_t i = code_point_count(box_->text()); i > 0; --i)
        {
            out += one;
        }
        return out;
    }
    // Read-only while disabled.
    [[nodiscard]] bool text_value_read_only() const override
    {
        return !box_->is_enabled();
    }
    // Replaces the password as if typed (fires `on_password_changed()`); false while disabled.
    bool set_text_value(std::string_view value) override;
};

/// A field for a password: the text is masked, and can be peeked at with the reveal button.
/// Builds a ui::PasswordBoxControl.
///
/// Example:
/// @code
/// void sign_in(const std::string& password); // the app's own
/// std::string secret;
///
/// window.set_content(ui::PasswordBox{
///     .header              = "Password",
///     .placeholder_text    = "Enter your password",
///     .max_length          = 64,
///     .width               = 240,
///     .on_password_changed = [&](const std::string& p) { secret = p; },
///     .on_submit           = [&] { sign_in(secret); },
/// });
/// @endcode
struct PasswordBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<PasswordBox>` points at.
    using control_type = PasswordBoxControl;

    // ---- properties ----
    /// The label above the control. Empty draws none.
    Binding<std::string> header{};
    /// Secondary text under the field, such as what to enter or why; empty draws none. Screen
    /// readers offer it as help text.
    Binding<std::string> description{};
    /// What is wrong with the value: non-empty shows it in the description's place in the critical
    /// color and draws the border in that color, and screen readers report the field invalid.
    /// The field shows exactly what it is given, so bind it to a check of the value and decide
    /// there when it shows (after the first submit, say).
    Binding<std::string> error{};
    /// The hint shown while the password is empty.
    Binding<std::string> placeholder_text{};
    /// The password (UTF-8); drawn masked unless revealed.
    TwoWayField<std::string> password{};
    /// The most characters the user can type or paste; 0 is no limit.
    Binding<std::size_t> max_length{0};
    /// The code point drawn once per hidden character; U+25CF by default.
    Binding<char32_t> password_char{U'\x25CF'};
    /// When the password can be revealed; Peek by default.
    Binding<PasswordBoxRevealMode> reveal_mode{PasswordBoxRevealMode::Peek};

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
    /// Fires after every change the user makes, with the new password.
    Callback<void(const std::string&)> on_password_changed{};
    /// Fired on Enter. Without it Enter reaches the window's default button.
    Action on_submit{};
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
    ControlOut<PasswordBox> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PasswordBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

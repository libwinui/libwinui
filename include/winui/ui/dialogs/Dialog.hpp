// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <coroutine>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// Which button of a dialog is the default one.
enum class DialogDefaultButton
{
    None,      ///< No default button: Enter clicks nothing, no button is accented.
    Primary,   ///< The primary (leftmost) button.
    Secondary, ///< The secondary button.
    Close,     ///< The close (cancel) button.
};

/// What closed the dialog. The Close button and Escape report Close; hide() and
/// Window::close_modal() report None.
enum class DialogResult
{
    None,      ///< Closed from outside: hide() or Window::close_modal().
    Primary,   ///< Closed by the primary button.
    Secondary, ///< Closed by the secondary button.
    Close,     ///< Closed by the Close button or Escape.
};

/// What a Dialog's `on_closing` gets: the result the close reports. The handler returns
/// ui::Cancel to keep the dialog open.
struct DialogClosingArgs
{
    DialogResult result = DialogResult::None; ///< Which button (or Escape) is closing it.
};

/// A modal dialog over the whole window, with a title, arbitrary content and up to three
/// buttons.
///
/// It isn't placed in the tree. Hand it to Window::show_modal(), which takes ownership and routes
/// all input to it until it closes. Tab moves only between the dialog's controls, and focus goes
/// back where it was afterward. The buttons are always Primary, Secondary, Close from left to
/// right, whatever order they were set in.
///
/// The default_button() is accented and clicked by an Enter that nothing else in the dialog uses.
/// Escape clicks the Close button, or closes with Close when there is none. Every button and
/// Escape close the dialog. on_closing() runs first and may cancel. Then the window takes the
/// dialog down, and on_closed() reports the result once it is gone. Window::close_modal() closes
/// it from outside, reporting None. With no default button set, the primary button is the
/// default when there is one. A dialog whose primary button destroys something sets Close or
/// None instead.
///
/// The dialog is at least 320x184 and at most 548x756 DIPs, unless its `width`, `max_width`,
/// `height` or `max_height` say otherwise. It fades in over 250 ms.
///
/// Example:
/// @code
/// auto dialog = std::make_unique<ui::DialogControl>();
/// dialog->set_title("Close without saving?");
/// dialog->set_content(std::make_unique<ui::TextControl>("Your changes will be lost."));
/// dialog->set_primary_button("Close", nullptr);
/// dialog->set_close_button("Cancel", nullptr);
/// dialog->set_default_button(ui::DialogDefaultButton::Close);
/// dialog->on_closed([&](ui::DialogResult result) {
///     if (result == ui::DialogResult::Primary)
///         quit();
/// });
/// window.show_modal(std::move(dialog));
///
/// // Declaratively:
/// bool busy = false;
/// window.show_modal(ui::Dialog{
///     .title          = "Close without saving?",
///     .default_button = ui::DialogDefaultButton::Close,
///     .primary        = {.text = "Close"},
///     .close          = {.text = "Cancel"},
///     .on_closing     = [&](const ui::DialogClosingArgs& args)
///     { return args.result == ui::DialogResult::Primary && busy ? ui::Cancel : ui::Proceed; },
///     .on_closed = [&](ui::DialogResult result)
///     {
///         if (result == ui::DialogResult::Primary)
///             quit();
///     },
///     .content = ui::Text{.text = "Your changes will be lost."},
/// });
/// @endcode
class DialogControl final : public Element
{
public:
    /// An empty dialog: no title, no content, no buttons.
    DialogControl();

    /// Sets the heading above the content (UTF-8), drawn 20 DIPs, semibold and wrapping.
    void set_title(std::string title);
    /// The title; empty by default. Screen readers also use it as the name when none is set.
    [[nodiscard]] const std::string& title() const
    {
        return title_->text();
    }

    /// Takes ownership of the body content and returns a non-owning observer to it, the same
    /// convention as StackControl::add(). Each further call appends below the previous content.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "DialogControl content must be an Element");
        return body_->add(std::move(content));
    }

    /// Sets the primary (leftmost) button, which closes with Primary. Each button's click runs
    /// `on_click` (if any), then closes the dialog with its result. Calling one again replaces
    /// that button. The visual order is fixed whatever order these are called in.
    /// Returns the button as a non-owning observer, owned by the dialog. `on_click` may be null.
    ButtonControl* set_primary_button(std::string text, Callback<void()> on_click);
    /// Sets the secondary button, which closes with Secondary.
    ButtonControl* set_secondary_button(std::string text, Callback<void()> on_click);
    /// Sets the close button, also clicked by Escape, which closes with Close.
    ButtonControl* set_close_button(std::string text, Callback<void()> on_click);

    /// The primary button, or nullptr when none was set.
    [[nodiscard]] ButtonControl* primary_button() const
    {
        return primary_.get();
    }
    /// The secondary button, or nullptr when none was set.
    [[nodiscard]] ButtonControl* secondary_button() const
    {
        return secondary_.get();
    }
    /// The close button, or nullptr when none was set.
    [[nodiscard]] ButtonControl* close_button() const
    {
        return close_.get();
    }

    /// Sets the default button. It is drawn accented and clicked by an Enter that the focused
    /// element does not use itself (a Button, a list or a TextBox with a submit handler keep
    /// theirs). While focus is on one of the dialog's own buttons the accent follows focus
    /// instead. `std::nullopt` (the default) makes the primary button the default when there is
    /// one, and no button otherwise.
    void set_default_button(std::optional<DialogDefaultButton> button);
    /// The default button in effect: the one set, else Primary when there is a primary button,
    /// else None.
    [[nodiscard]] DialogDefaultButton default_button() const;

    /// Close with `None` (on_closing still runs, and may cancel).
    void hide();

    /// What the last close reported; `None` until one happened.
    [[nodiscard]] DialogResult result() const
    {
        return result_;
    }

    /// Raised before every close with its result; return ui::Cancel to keep the
    /// dialog open (a Primary click on a form that does not validate, say).
    void on_closing(Callback<Decision(const DialogClosingArgs&)> handler)
    {
        on_closing_ = std::move(handler);
    }

    /// Raised once the dialog is closed and out of the window, with what closed it. The dialog is
    /// destroyed by then, so the handler may show the next one. A dialog replaced by another
    /// show_modal() does not raise it.
    void on_closed(Callback<void(DialogResult)> handler)
    {
        on_closed_ = std::move(handler);
    }
    /// Visits the title, the content host and the shown buttons, in visual order.
    void for_each_child(ChildVisitor visit) const override;
    /// Adds the content's focusable controls, then the shown buttons, to the dialog's tab order.
    /// The title is never a stop.
    void collect_focusables(std::vector<Element*>& out) override;
    /// Where focus starts: the first focusable thing in the content, else the default button.
    [[nodiscard]] Element* default_focus() override;

    /// The opening fade's opacity, rising linearly from 0 to 1 over 250 ms. The dimmed backdrop
    /// behind the dialog fades in with it. There is no closing fade. Window::close_modal() removes
    /// the dialog and restores focus at once.
    [[nodiscard]] float transition_opacity() const override
    {
        return open_ms_ >= OpenTransitionMs || !animations_enabled() ? 1.0f : static_cast<float>(open_ms_ / OpenTransitionMs);
    }

    // ---- automation ----
    /// Always AutomationRole::Pane.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Pane;
    }
    /// title(), used when Accessibility::name is not set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return title();
    }

protected:
    // ---- Element ----
    /// The frame, title, content and button area, clamped between 320x184 and 548x756 DIPs. A
    /// width or max width set on the dialog replaces the 548, and a height or max height the 756.
    /// When the dialog is wider than 548, the buttons keep their size at 548 and group at the right.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the title and content, with 24 DIPs of padding and 12 DIPs apart, and the buttons
    /// in the button area. Three buttons take a third each, two a half each, and one alone the
    /// right half.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the frame, the title and content background, the button area and the children.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the open fade.
    void tick_override(Duration delta) override;


private:
    // The live elements inside the content only (a ComboBox, a tooltip): neither the dialog nor
    // its title and buttons ever own a popup.
    void collect_live(std::vector<Element*>& out) override;
    // Escape: the Close button's click when there is an enabled one, else a close with `Close`.
    void dismiss();

    std::unique_ptr<ButtonControl> make_button(std::string text, Callback<void()> on_click, DialogResult result);
    void                           close_with(DialogResult result);
    [[nodiscard]] ButtonControl*   default_button_ptr() const;
    void                           update_button_styles(); // the default button's accent

    [[nodiscard]] std::vector<ButtonControl*> visible_buttons() const;

    // Theme::anim_normal as a literal: tick_override() has no Theme.
    static constexpr double OpenTransitionMs = 250.0;
    double                  open_ms_         = 0.0; // advanced by tick_override()

    std::unique_ptr<TextControl>   title_;
    std::unique_ptr<StackControl>  body_;
    std::unique_ptr<ButtonControl> primary_;
    std::unique_ptr<ButtonControl> secondary_;
    std::unique_ptr<ButtonControl> close_;

    std::optional<DialogDefaultButton> default_button_; // unset: Primary when there is one
    DialogResult                       result_  = DialogResult::None;
    bool                               closing_ = false; // re-entrancy guard for close_with()

    // Cached by measure_override() for arrange_override() and render().
    mutable float title_height_     = 0.0f;
    mutable float button_height_    = 0.0f;
    mutable float separator_height_ = 0.0f;
    mutable float command_height_   = 0.0f; // 0 with no buttons: the command area is collapsed
    mutable float border_           = 1.0f;

    // Cached by arrange_override() for render().
    Rect top_rect_{};     // title + content area, inside the frame
    Rect command_rect_{}; // the button area, inside the frame; empty with no buttons

    Callback<Decision(const DialogClosingArgs&)> on_closing_;
    Callback<void(DialogResult)>                 on_closed_;
    // Set by the Window that shows this dialog: closes it there.
    Callback<void()> close_modal_;

    friend class Window; // installs close_modal_, raises on_closed_ once the dialog is gone

protected:
    /// Enter the focused element had no use for: the default button's click, if it is enabled.
    bool key_override(const KeyEvent& e) override;
};

/// One of a dialog's up-to-three buttons. Left default (empty text) means "no such button".
///
/// Example:
/// @code
/// ui::Property<bool> can_save{false};
/// ui::DialogButton   save_button{ .text = "Save", .enabled = can_save, .on_click = [&] { save(); } };
/// @endcode
struct DialogButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The button's caption; empty leaves the button out.
    std::string text{};
    /// Whether the button takes clicks; false grays it out.
    Binding<bool> enabled{true};
    /// Fired on click, before the dialog starts closing.
    Action on_click{};

    /// True when the button exists, which means it has a caption.
    [[nodiscard]] explicit operator bool() const
    {
        return !text.empty();
    }
};

/// A modal dialog. Builds a ui::DialogControl, which a caller then
/// hands to Window::show_modal().
///
/// Every button and Escape close the dialog by themselves. on_closing runs first and may cancel,
/// for example while a save is still running. Then on_closed reports the result once the dialog
/// is gone.
///
/// Example:
/// @code
/// window.show_modal(ui::Dialog{
///     .title          = "Close without saving?",
///     .default_button = ui::DialogDefaultButton::Close,
///     .primary        = {.text = "Close"},
///     .close          = {.text = "Cancel"},
///     .on_closed      = [&](ui::DialogResult result) {
///         if (result == ui::DialogResult::Primary)
///             quit();
///     },
///     .content        = ui::Text{.text = "Your changes will be lost."},
/// });
/// @endcode
struct Dialog
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Dialog>` points at.
    using control_type = DialogControl;

    // ---- properties ----
    /// The heading above the content.
    Binding<std::string> title{};
    /// The default button: accented, and clicked by an Enter nothing else in the dialog uses.
    /// Unset makes it the primary button when there is one, and no button otherwise.
    Binding<std::optional<DialogDefaultButton>> default_button{};
    /// Always shown Primary, Secondary, Close from left to right; an empty text leaves one out.
    DialogButton primary{};
    /// The middle button; closes with DialogResult::Secondary.
    DialogButton secondary{};
    /// The right-hand button, also clicked by Escape; closes with DialogResult::Close.
    DialogButton close{};

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
    /// Gets the result of every close before it happens; return ui::Cancel to keep the dialog open.
    Callback<Decision(const DialogClosingArgs&)> on_closing{};
    /// Fired once the dialog is closed and gone, with what closed it.
    Callback<void(DialogResult)> on_closed{};
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
    /// Out: the handle that receives the built control. The window owns the control once shown,
    /// until it closes.
    ControlOut<Dialog> control{};

    // ---- children ----
    /// The body between the title and the buttons.
    Spec content{};

    /// Consumes the spec and returns the built ui::DialogControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

class Window;

/// What `co_await ui::show_dialog(window, spec)` waits on; see show_dialog().
class DialogAwaiter
{
public:
    /// Holds `spec` until the coroutine suspends.
    DialogAwaiter(Window& window, Dialog spec) : window_(&window), spec_(std::move(spec))
    {
    }
    /// Never ready: the dialog is shown when the coroutine suspends.
    [[nodiscard]] bool await_ready() const noexcept
    {
        return false;
    }
    /// Shows the dialog and hands `coroutine` to its close.
    void await_suspend(std::coroutine_handle<> coroutine);
    /// What closed the dialog.
    [[nodiscard]] DialogResult await_resume() const noexcept
    {
        return result_;
    }

private:
    Window*      window_;
    Dialog       spec_;
    DialogResult result_ = DialogResult::None;
};

/// Shows `spec` modally over `window` and lets a coroutine wait for the answer.
/// `co_await ui::show_dialog(window, spec)` gives the DialogResult the dialog closed with, so a
/// flow of questions reads top to bottom without nested on_closed handlers. The spec's own
/// on_closing, which may still cancel, and on_closed still run. on_closed runs before the
/// coroutine goes on.
///
/// The coroutine resumes on the UI thread inside the window's next tick(), as after
/// on_ui_thread(). When the dialog goes without an answer, the coroutine is dropped instead and
/// its frame destroyed without resuming. That happens when another modal replaces the dialog,
/// when the Window is destroyed with it open, or when the window is closed by the time the answer
/// would resume it. A coroutine that has to clean up does it in a destructor, as with
/// on_ui_thread(). Call it on the UI thread, from a coroutine such as a ui::FireAndForget.
///
/// With MSVC, compute texts that come from a function call into local variables before the
/// `co_await`. MSVC can destroy a temporary of a `co_await` expression too early, and the call
/// that reads it crashes.
///
/// Example:
/// @code
/// ui::FireAndForget delete_files(ui::Window& window, ui::Property<std::string>& status)
/// {
///     const ui::DialogResult answer = co_await ui::show_dialog(window, ui::Dialog{
///         .title          = "Delete 3 files?",
///         .default_button = ui::DialogDefaultButton::Close,
///         .primary        = {.text = "Delete"},
///         .close          = {.text = "Cancel"},
///         .content        = ui::Text{.text = "They go to the recycle bin."},
///     });
///     if (answer != ui::DialogResult::Primary)
///         co_return;
///     status = "Deleted";
/// }
/// @endcode
[[nodiscard]] inline DialogAwaiter show_dialog(Window& window, Dialog spec)
{
    return DialogAwaiter{window, std::move(spec)};
}

} // namespace ui

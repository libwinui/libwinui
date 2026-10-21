// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// How serious an InfoBar's message is; picks the bar's background color and its status icon.
enum class InfoBarSeverity
{
    Informational, ///< neutral background, info icon (the default)
    Success,       ///< success background, check-mark icon
    Warning,       ///< caution background, warning icon
    Error,         ///< critical background, error icon
};

/// An inline, closable banner that shows an app-wide status message with a severity color and icon.
///
/// A freshly built bar is closed: it takes no room and draws nothing until set_open(true). Title,
/// message and the optional action button sit on one line when they fit and stack (wrapping at whole
/// words) when they don't; the bar is at least 48 DIPs tall. Optional custom content (set_content())
/// gets a row of its own below them. The InfoBar itself is never a tab stop. Its action button,
/// its content's controls and its close button are.
///
/// Closing runs in a fixed order: a close-button click first raises on_close_button_click(),
/// then on_closing() (return ui::Cancel to keep it open, leaving is_open() true). Only a close
/// that goes ahead flips is_open() to false and raises on_closed(). set_open(false) runs the same
/// on_closing() and on_closed() sequence with reason Programmatic. Reopening raises nothing.
///
/// Example:
/// @code
/// // Declarative, with the open state in a Property:
/// ui::Property<bool> open{true};
/// window.set_content(ui::InfoBar{
///     .title     = "Update available",
///     .message   = "Restart the app to apply the update.",
///     .is_open   = open,
///     .severity  = ui::InfoBarSeverity::Success,
///     .on_closed = [&open](ui::PopupCloseReason) { open = false; },
///     .action_button_content = ui::Button{.text = "Restart", .on_click = [] { restart_app(); }},
/// });
///
/// // Imperative:
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* bar   = stack->add(std::make_unique<ui::InfoBarControl>("Offline", "Changes will sync later.",
///                                                            ui::InfoBarSeverity::Warning, true));
/// bar->on_closing([](const ui::PopupClosingArgs& args) {
///     return args.reason == ui::PopupCloseReason::Programmatic ? ui::Cancel : ui::Proceed; // keep it open
/// });
/// bar->set_action_button_content(std::make_unique<ui::ButtonControl>("Retry"))->on_click([] { save(); });
/// @endcode
class InfoBarControl final : public Element
{
public:
    /// A closed bar, which takes no room and draws nothing until opened.
    InfoBarControl();
    /// A bar with its title, message and severity set, open or (by default) closed.
    explicit InfoBarControl(std::string     title,
                            std::string     message,
                            InfoBarSeverity severity = InfoBarSeverity::Informational,
                            bool            is_open  = false);

    // ---- content ----
    /// The bold title, first on the line; empty by default.
    [[nodiscard]] const std::string& title() const
    {
        return title_;
    }
    /// Sets the title (UTF-8). Updates the layout.
    void set_title(std::string title);
    /// The message after the title, wrapped at whole words when the bar stacks.
    [[nodiscard]] const std::string& message() const
    {
        return message_;
    }
    /// Sets the message (UTF-8). Updates the layout.
    void set_message(std::string message);

    /// The severity; Informational by default.
    [[nodiscard]] InfoBarSeverity severity() const
    {
        return severity_;
    }
    /// Sets the severity, changing the background and icon; redraws.
    void set_severity(InfoBarSeverity severity);

    /// Whether the severity icon shows at the left; true by default.
    [[nodiscard]] bool is_icon_visible() const
    {
        return is_icon_visible_;
    }
    /// Shows or hides the severity icon. Updates the layout.
    void set_icon_visible(bool visible);

    // ---- open/close ----
    /// Whether the bar shows; false by default.
    [[nodiscard]] bool is_open() const
    {
        return is_open_;
    }
    /// Setting false runs the closing sequence, which on_closing() can cancel, with reason
    /// Programmatic. Setting true always reopens. Does nothing, and raises nothing, if the value
    /// is unchanged.
    void set_open(bool is_open);

    /// Whether the close button shows; true by default.
    [[nodiscard]] bool is_closable() const
    {
        return is_closable_;
    }
    /// Shows/hides the owned close button; true by default.
    void set_closable(bool closable);

    /// A disabled bar grays out its close button, which then takes no input. The
    /// action button and the content keep their own state.
    [[nodiscard]] bool enabled() const
    {
        return close_button_->is_enabled();
    }

    // ---- action button ----
    /// Takes ownership of the action element (normally a Button, or one with
    /// `ButtonVariant::Hyperlink`) and returns a non-owning observer. It sits after the message.
    template <typename T>
    T* set_action_button_content(std::unique_ptr<T> action)
    {
        static_assert(std::is_base_of_v<Element, T>, "InfoBar action button must be an Element");
        T* observer = action.get();
        if (observer != nullptr)
        {
            set_parent_of(*observer, this);
        }
        action_ = std::move(action);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The action element (non-owning), or nullptr.
    [[nodiscard]] Element* action_button_content() const
    {
        return action_.get();
    }
    /// Removes and destroys the action element. Updates the layout.
    void clear_action_button_content();

    // ---- custom content ----
    /// Takes ownership of custom content and returns a non-owning observer; a null `content`
    /// clears it. The content gets the full width between the icon and the close button, on its
    /// own row below the title, message and action. With no title, message or action it moves up
    /// into their row instead, centered against the icon and close button. The bar never stacks or
    /// wraps it as it does the message. The content lays itself out in the width it is given.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "InfoBar content must be an Element");
        T* observer = content.get();
        if (observer != nullptr)
        {
            set_parent_of(*observer, this);
        }
        content_      = std::move(content);
        content_rect_ = Rect{};
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The custom content (non-owning), or nullptr.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }
    /// Removes and destroys the custom content. Updates the layout.
    void clear_content();

    /// Fired on every close-button click, before on_closing(), whether or not the close is then
    /// canceled.
    void on_close_button_click(Callback<void()> handler)
    {
        on_close_button_click_ = std::move(handler);
    }
    /// Runs before is_open() flips to false. Returning ui::Cancel keeps the bar open, and
    /// is_open() stays true throughout.
    void on_closing(Callback<Decision(const PopupClosingArgs&)> handler)
    {
        on_closing_ = std::move(handler);
    }
    /// Fired once a close goes ahead, that is when on_closing() did not cancel it.
    void on_closed(Callback<void(PopupCloseReason)> handler)
    {
        on_closed_ = std::move(handler);
    }

    /// The close button's own rect from the most recent arrange(), for hit-testing. Empty while
    /// the close button is hidden.
    [[nodiscard]] const Rect& close_button_bounds() const
    {
        return close_button_rect_;
    }
    /// The title's rect from the most recent arrange().
    [[nodiscard]] const Rect& title_bounds() const
    {
        return title_rect_;
    }
    /// The message's rect from the most recent arrange().
    [[nodiscard]] const Rect& message_bounds() const
    {
        return message_rect_;
    }
    /// The slot the custom content was last arranged into (before its own margin and alignment):
    /// below the title row, or the title row itself when the bar has no title, message or action.
    /// Empty without content.
    [[nodiscard]] const Rect& content_bounds() const
    {
        return content_rect_;
    }
    /// Whether the last measure() stacked title, message and action.
    [[nodiscard]] bool is_vertical() const
    {
        return vertical_;
    }

    // ---- the owned action, content and close button, gated on is_open_ ----
    /// Every owned child that currently takes part in input, focus and rendering, in tab order:
    /// the action, then the custom content, while open; the close button while open and
    /// closable.
    void for_each_child(ChildVisitor visit) const override
    {
        if (action_active())
        {
            visit(*action_);
        }
        if (content_active())
        {
            visit(*content_);
        }
        if (close_button_active())
        {
            visit(*close_button_);
        }
    }

    // ---- automation ----
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Pane;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return title_;
    }

protected:
    // ---- layout ----
    /// At least 48 DIPs tall, taller when the content stacks (see is_vertical()) or custom
    /// content adds its row. Zero size while closed.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- focus ----
    // InfoBar itself is never a tab stop. Only the action button (while open)
    // and the close button (while open and closable) are, in that order (for_each_child()).

    // ---- input ----

    // ---- render ----
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // Itself, then the custom content's live elements while open; the action and close button
    // are not offered (an action that opens a flyout of its own would need them to be).
    void collect_live(std::vector<Element*>& out) override
    {
        out.push_back(this);
        if (content_active())
        {
            detail::TreeAccess::collect_live(*content_, out);
        }
    }
    [[nodiscard]] bool close_button_active() const
    {
        return is_open_ && is_closable_;
    }
    [[nodiscard]] bool action_active() const
    {
        return is_open_ && action_ != nullptr;
    }
    [[nodiscard]] bool content_active() const
    {
        return is_open_ && content_ != nullptr;
    }
    // Fires on_close_button_click_ first for a close-button click, then the cancelable
    // on_closing_. Only a close that goes ahead flips is_open_ and fires on_closed_.
    void close(PopupCloseReason reason);

    std::string     title_;
    std::string     message_;
    InfoBarSeverity severity_        = InfoBarSeverity::Informational;
    bool            is_open_         = false;
    bool            is_closable_     = true;
    bool            is_icon_visible_ = true;

    std::unique_ptr<ButtonControl> close_button_;
    std::unique_ptr<Element>       action_;
    std::unique_ptr<Element>       content_;

    // Populated by measure_override(), consumed by arrange_override(), which has neither the Theme nor a
    // TextMeasurer: each item's measured size, and which way the title row stacked them.
    mutable Size  title_size_{};
    mutable Size  message_size_{};
    mutable Size  action_size_{};
    mutable Size  content_size_{};
    mutable float first_row_height_ = 0.0f; // the first row: icon, panel, close button
    mutable bool  vertical_         = false;
    mutable bool  action_hyperlink_ = false;
    mutable bool  has_banner_       = false; // a title, message or action keeps the content in row 2
    Rect          icon_rect_{};
    Rect          title_rect_{};
    Rect          message_rect_{};
    Rect          action_rect_{};
    Rect          content_rect_{};
    Rect          close_button_rect_{};

    Callback<void()>                            on_close_button_click_;
    Callback<Decision(const PopupClosingArgs&)> on_closing_;
    Callback<void(PopupCloseReason)>            on_closed_;

protected:
    void enabled_changed() override;
    bool key_override(const KeyEvent& e) override;
};

/// An inline status banner. Builds a ui::InfoBarControl. Closed by default, matching the control.
///
/// The handlers are installed before `is_open` is bound, so a bound `is_open` flipping back to
/// false raises `.on_closing`/`.on_closed` like any other close.
///
/// Example:
/// @code
/// ui::Property<bool> update_ready{true};
///
/// window.set_content(ui::InfoBar{
///     .title         = "Update available",
///     .message       = "Restart to install.",
///     .is_open       = update_ready, // two-way: closing the bar writes false
///     .severity      = ui::InfoBarSeverity::Success,
///     .action_button_content = ui::Button{.text = "Restart", .on_click = [&] { restart_app(); }},
/// });
/// @endcode
struct InfoBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<InfoBar>` points at.
    using control_type = InfoBarControl;

    // ---- properties ----
    /// The bold title, first on the line.
    Binding<std::string> title{};
    /// The message after the title.
    Binding<std::string> message{};
    /// A closed bar takes no room. `ui::bind(prop)` is two-way: closing the bar writes
    /// false into the property, and a false that `on_closing` cancels is put back to true.
    TwoWayField<bool> is_open{false};
    /// Picks the background color and the status icon.
    Binding<InfoBarSeverity> severity{InfoBarSeverity::Informational};
    /// Shows the close button.
    Binding<bool> closable{true};
    /// Shows the severity icon.
    Binding<bool> icon_visible{true};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays out the close button.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The close button was clicked, before `.on_closing`.
    Action on_close_button_click{};
    /// Why it is about to close; return ui::Cancel to keep it open.
    Callback<Decision(const PopupClosingArgs&)> on_closing{};
    /// The bar has closed, and why.
    Callback<void(PopupCloseReason)> on_closed{};
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
    ControlOut<InfoBar> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// A button placed after the message, usually a ui::Button, with
    /// `ButtonVariant::Hyperlink` for a link.
    Spec action_button_content{};
    /// Custom content on a row of its own below the title, message and action (in
    /// their place when all three are empty), across the full width up to the close button.
    Spec content{};

    /// Consumes the spec and returns the built ui::InfoBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui

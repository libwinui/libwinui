// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/IconSource.hpp"

#include <concepts>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// What the command does, given the invoking control's CommandParameter.
/// Takes a callable of either `void()` or `void(std::string_view parameter)`, so a handler with
/// no use for the parameter may leave it out of its signature. Move-only, like ui::Callback.
///
/// Example:
/// @code
/// // Reads the invoking control's `.command_parameter`:
/// ui::Command remove{
///     .text    = "Remove",
///     .icon    = ui::Symbol::Remove,
///     .execute = [&](std::string_view item) { remove_item(item); },
/// };
/// // Has no use for it, so leaves it out:
/// ui::Command refresh{
///     .text    = "Refresh",
///     .icon    = ui::Symbol::Refresh,
///     .execute = [&] { reload_list(); },
/// };
///
/// window.set_content(ui::CommandBar{
///     .primary = {
///         ui::AppBarButton{.command = remove, .command_parameter = "List item 3"}, // remove_item("List item 3")
///         ui::AppBarButton{.command = refresh},
///     },
/// });
/// @endcode
class CommandHandler
{
public:
    /// An empty handler: calling it does nothing and it converts to false.
    CommandHandler() = default;
    /// Wraps `handler`, a callable of `void()` or `void(std::string_view)`; a `mutable` lambda
    /// may keep state between runs.
    template <typename F>
        requires std::invocable<F&> || std::invocable<F&, std::string_view>
    CommandHandler(F handler) // NOLINT(google-explicit-constructor): a lambda is a handler
    {
        if constexpr (std::invocable<F&, std::string_view>)
        {
            fn_ = std::move(handler);
        }
        else
        {
            fn_ = [handler = std::move(handler)](std::string_view) mutable { handler(); };
        }
    }

    /// Runs the handler with `parameter`; a no-op when empty.
    void operator()(std::string_view parameter = {}) const
    {
        if (fn_)
        {
            fn_(parameter);
        }
    }
    /// True when a handler was given.
    [[nodiscard]] explicit operator bool() const
    {
        return static_cast<bool>(fn_);
    }

private:
    mutable Callback<void(std::string_view)> fn_; // run from a const command; may hold a mutable lambda
};

/// What a Command's fields hold right now (Command::current()). A bound field holds its latest
/// value, a plain field its fixed value.
struct CommandValues
{
    /// The command's text.
    std::string text{};
    /// The command's icon.
    IconSource icon{};
    /// The command's shortcut.
    KeyCombo shortcut{};
    /// The command's description.
    std::string description{};
    /// Whether the command may run.
    bool enabled = true;
};

struct Command;
class CommandRef;

namespace detail
{
// A Command's current values, the subscriptions that keep them current, and the CommandRefs that
// point at it. Connected the first time anything reads the command. A copy or a move starts
// disconnected and with no refs: the subscriptions write into, and the refs point at, the object
// they were made for.
class CommandLive
{
public:
    CommandLive() = default;
    CommandLive(const CommandLive&) noexcept
    {
    }
    // The command's fields may have changed with the assignment: read them again on the next use
    // and tell whoever watches this command. Its refs stay: they point at this object.
    CommandLive& operator=(const CommandLive&) noexcept
    {
        subscriptions_.clear();
        connected_ = false;
        changed_.raise();
        return *this;
    }
    // Every CommandRef to the command goes empty, then its watchers hear of it once more.
    ~CommandLive();

private:
    friend struct ui::Command;
    friend class ui::CommandRef;
    bool                      connected_ = false;
    CommandValues             values_{};
    Signal                    changed_{};
    std::vector<Subscription> subscriptions_{};
    CommandRef*               refs_ = nullptr; // intrusive list of the refs pointing here
};
} // namespace detail

/// What a control's `.command` field holds: a non-owning reference to a ui::Command, written as
/// the command itself (`.command = save`), or empty. Either end may go first: a CommandRef whose
/// command was destroyed is empty, and the controls holding it act as if they had none.
///
/// It follows the Command object it was given. A copy of the command, or one it was moved into,
/// starts with no refs.
class CommandRef
{
public:
    /// No command.
    CommandRef() noexcept = default;
    /// No command.
    CommandRef(std::nullptr_t) noexcept // NOLINT(google-explicit-constructor)
    {
    }
    /// A reference to `command`; implicit, so a field takes `.command = save`.
    CommandRef(const Command& command) noexcept; // NOLINT(google-explicit-constructor)
    /// Another reference to the same command.
    CommandRef(const CommandRef& other) noexcept;
    /// Points at `other`'s command instead.
    CommandRef& operator=(const CommandRef& other) noexcept;
    ~CommandRef();

    /// The command, or nullptr when there is none or it was destroyed.
    [[nodiscard]] const Command* get() const noexcept
    {
        return command_;
    }
    /// The command, or nullptr: a CommandRef reads like the pointer it replaces.
    operator const Command*() const noexcept // NOLINT(google-explicit-constructor)
    {
        return command_;
    }
    /// The command. Only use it while there is one.
    const Command* operator->() const noexcept
    {
        return command_;
    }

private:
    friend class detail::CommandLive;
    void link(const Command* command) noexcept;
    void unlink() noexcept;

    const Command* command_ = nullptr;
    CommandRef*    prev_    = nullptr;
    CommandRef*    next_    = nullptr;
};

/// An action together with what every control invoking it would otherwise repeat: text, an icon,
/// a shortcut, a description and whether it may run.
///
/// Point an AppBarButton, MenuItem or SwipeItem at one, and each fills in whatever of those it
/// has not been given itself. It takes the text and the icon, and shows the description as its
/// tooltip. On an AppBarButton or a MenuItem, the shortcut fires app-wide while the control is in
/// the window, and a menu shows it as "Ctrl+D". A SwipeItem does not register the shortcut. A
/// Button, ToggleButton, DropdownButton or SplitButton runs the command, takes its enabled state
/// and uses its description as the tooltip when it has none, but keeps its own text.
///
/// Every field takes a value or a binding, and every control using the command follows a bound
/// field as it changes. With `.enabled = has_selection`, every button and menu item pointing at
/// the command is enabled and disabled with `has_selection`. A control's own `.enabled` still
/// applies, so the control is enabled only while both allow it.
///
/// Write the command down once and give it to every control that offers it: `.command = save`.
/// As with a Property, either the command or the controls may be destroyed first. A control whose
/// command was destroyed acts as if it had none. A plain field is a value fixed at construction,
/// as on a spec, so bind a field that should change.
///
/// Example:
/// @code
/// ui::Property<std::string> output{};
/// ui::Property<bool>        has_item{true};
/// ui::Command favorite{
///     .text        = "Add to favorites",
///     .icon        = ui::Symbol::Favorite,
///     .shortcut    = ui::Ctrl + ui::Key::D,
///     .description = "Mark the selected item as a favorite",
///     .enabled     = has_item,
///     .execute     = [&](std::string_view item) { output = "Favorited " + std::string(item); },
/// };
///
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::Button{.text = "Favorite", .command = favorite, .command_parameter = "Item 1"},
///         ui::Text{.text = output},
///     },
/// });
/// has_item = false; // the button disables itself, and Ctrl+D does nothing
/// @endcode
struct Command
{
    /// UTF-8 text, used by an app-bar button, a menu item or a swipe item that has no text of its
    /// own.
    Binding<std::string> text{};

    /// The invoking control's icon when it has none; empty by default.
    Binding<IconSource> icon{};

    /// The shortcut. It fires app-wide while a control offering the command is in the window, and
    /// the control shows it as its shortcut text.
    Binding<KeyCombo> shortcut{};

    /// The invoking control's tooltip, unless it has one.
    Binding<std::string> description{};

    /// Whether the command may run. Every control using it is disabled while this is false, on
    /// top of its own `.enabled`, and operator() does nothing.
    Binding<bool> enabled{true};

    /// Run by operator() with the invoking control's `.command_parameter`, while `enabled`.
    CommandHandler execute{};

    /// What the fields hold right now, with each bound field's latest value.
    [[nodiscard]] const CommandValues& current() const;

    /// Calls `fn` after any bound field changes. The returned Subscription must be kept for `fn`
    /// to keep firing; a control using the command keeps one to redraw.
    [[nodiscard]] Subscription observe(Callback<void()> fn) const;

    /// Runs `execute` with `parameter` while the command is enabled; otherwise does nothing.
    void operator()(std::string_view parameter = {}) const;

    /// Internal: the current values, kept by the command itself. Never set it.
    mutable detail::CommandLive live_{};
};

/// The sixteen common commands standard_command() can pre-fill, plus None.
enum class StandardCommand
{
    None,      ///< No standard command: an empty Command.
    Cut,       ///< "Cut", Ctrl+X.
    Copy,      ///< "Copy", Ctrl+C.
    Paste,     ///< "Paste", Ctrl+V.
    SelectAll, ///< "Select All", Ctrl+A.
    Delete,    ///< "Delete", the Delete key.
    Share,     ///< "Share", no shortcut.
    Save,      ///< "Save", Ctrl+S.
    Open,      ///< "Open", Ctrl+O.
    Close,     ///< "Close", Ctrl+W (Symbol::Cancel).
    Pause,     ///< "Pause".
    Play,      ///< "Play".
    Stop,      ///< "Stop".
    Forward,   ///< "Forward".
    Backward,  ///< "Backward".
    Undo,      ///< "Undo", Ctrl+Z.
    Redo,      ///< "Redo", Ctrl+Y.
};

/// A Command with the standard text, symbol, shortcut and description of `kind`, running
/// `execute` while `enabled`. Delete is "Delete", Symbol::Delete, the Delete key and "Delete the
/// selected content". The text and description come from strings().commands and follow
/// set_strings(). Any field set afterwards, before a control uses the command, wins.
///
/// Example:
/// @code
/// ui::Property<bool> has_selection{false};
/// // Text "Delete", Symbol::Delete, the Delete key and the description come with the kind;
/// // the app adds what deleting means and when it is possible.
/// const ui::Command del = ui::standard_command(ui::StandardCommand::Delete,
///                                              [&](std::string_view item) { remove_item(item); },
///                                              has_selection);
///
/// // The button's text, icon, tooltip, shortcut and enabled state all come from `del`:
/// window.set_content(ui::CommandBar{
///     .primary = {ui::AppBarButton{.command = del, .command_parameter = "List item 3"}},
/// });
/// @endcode
[[nodiscard]] Command standard_command(StandardCommand kind, CommandHandler execute = {}, Binding<bool> enabled = true);

} // namespace ui

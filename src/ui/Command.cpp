// SPDX-License-Identifier: MIT
#include "winui/ui/Command.hpp"

#include "../core/StringsDetail.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Symbol.hpp"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ui
{

namespace
{
// Seeds `slot` from what `field` holds now and, for a bound field, keeps it current and tells `changed`.
template <typename T>
void follow_command_field(const Binding<T>& field, T& slot, Signal& changed, std::vector<Subscription>& subscriptions)
{
    slot = field.current();
    if (field.reactive())
    {
        Binding<T> copy = field; // subscribe() consumes nothing, but needs a binding of its own
        subscriptions.push_back(copy.subscribe(
            [&slot, &changed](T value)
            {
                slot = std::move(value);
                changed.raise();
            }));
    }
}
} // namespace

namespace detail
{
CommandLive::~CommandLive()
{
    for (CommandRef* ref = refs_; ref != nullptr;)
    {
        CommandRef* next = ref->next_;
        ref->command_    = nullptr;
        ref->prev_       = nullptr;
        ref->next_       = nullptr;
        ref              = next;
    }
    refs_ = nullptr;
    changed_.raise(); // the controls see their command gone
}
} // namespace detail

CommandRef::CommandRef(const Command& command) noexcept
{
    link(&command);
}

CommandRef::CommandRef(const CommandRef& other) noexcept
{
    link(other.command_);
}

CommandRef& CommandRef::operator=(const CommandRef& other) noexcept
{
    if (this != &other && command_ != other.command_)
    {
        unlink();
        link(other.command_);
    }
    return *this;
}

CommandRef::~CommandRef()
{
    unlink();
}

void CommandRef::link(const Command* command) noexcept
{
    command_ = command;
    if (command_ == nullptr)
    {
        return;
    }
    detail::CommandLive& live = command_->live_;
    next_                     = live.refs_;
    if (next_ != nullptr)
    {
        next_->prev_ = this;
    }
    live.refs_ = this;
}

void CommandRef::unlink() noexcept
{
    if (command_ == nullptr)
    {
        return;
    }
    if (prev_ != nullptr)
    {
        prev_->next_ = next_;
    }
    else
    {
        command_->live_.refs_ = next_;
    }
    if (next_ != nullptr)
    {
        next_->prev_ = prev_;
    }
    command_ = nullptr;
    prev_    = nullptr;
    next_    = nullptr;
}

const CommandValues& Command::current() const
{
    if (!live_.connected_)
    {
        live_.connected_ = true;
        follow_command_field(text, live_.values_.text, live_.changed_, live_.subscriptions_);
        follow_command_field(icon, live_.values_.icon, live_.changed_, live_.subscriptions_);
        follow_command_field(shortcut, live_.values_.shortcut, live_.changed_, live_.subscriptions_);
        follow_command_field(description, live_.values_.description, live_.changed_, live_.subscriptions_);
        follow_command_field(enabled, live_.values_.enabled, live_.changed_, live_.subscriptions_);
    }
    return live_.values_;
}

Subscription Command::observe(Callback<void()> fn) const
{
    (void)current(); // connected, so a bound field's change reaches `fn`
    return live_.changed_.observe(std::move(fn));
}

void Command::operator()(std::string_view parameter) const
{
    if (execute && current().enabled)
    {
        execute(parameter);
    }
}

Command standard_command(StandardCommand kind, CommandHandler execute, Binding<bool> enabled)
{
    // The symbol and shortcut of each kind; the text and description come from strings().
    using Field = Strings::CommandText Strings::StandardCommandsText::*;
    struct Entry
    {
        Field    field = nullptr; // null only for None, which has no text either
        Symbol   symbol{};
        KeyCombo shortcut;
    };
    const Entry entry = [kind]() -> Entry
    {
        using T = Strings::StandardCommandsText;
        switch (kind)
        {
            case StandardCommand::Cut:
                return {&T::cut, Symbol::Cut, Ctrl + Key::X};
            case StandardCommand::Copy:
                return {&T::copy, Symbol::Copy, Ctrl + Key::C};
            case StandardCommand::Paste:
                return {&T::paste, Symbol::Paste, Ctrl + Key::V};
            case StandardCommand::SelectAll:
                return {&T::select_all, Symbol::SelectAll, Ctrl + Key::A};
            case StandardCommand::Delete:
                return {&T::delete_selection, Symbol::Delete, Key::Delete};
            case StandardCommand::Share:
                return {&T::share, Symbol::Share, {}};
            case StandardCommand::Save:
                return {&T::save, Symbol::Save, Ctrl + Key::S};
            case StandardCommand::Open:
                return {&T::open, Symbol::OpenFile, Ctrl + Key::O};
            case StandardCommand::Close:
                return {&T::close, Symbol::Cancel, Ctrl + Key::W};
            case StandardCommand::Pause:
                return {&T::pause, Symbol::Pause, {}};
            case StandardCommand::Play:
                return {&T::play, Symbol::Play, {}};
            case StandardCommand::Stop:
                return {&T::stop, Symbol::Stop, {}};
            case StandardCommand::Forward:
                return {&T::forward, Symbol::Forward, {}};
            case StandardCommand::Backward:
                return {&T::backward, Symbol::Back, {}};
            case StandardCommand::Undo:
                return {&T::undo, Symbol::Undo, Ctrl + Key::Z};
            case StandardCommand::Redo:
                return {&T::redo, Symbol::Redo, Ctrl + Key::Y};
            case StandardCommand::None:
                break;
        }
        return {};
    }();

    Command command;
    if (entry.field != nullptr)
    {
        const Field field = entry.field;
        command.text      = detail::bind_strings([field](const Strings& s) { return (s.commands.*field).text; });
        command.icon      = entry.symbol;
        command.shortcut  = entry.shortcut;
        command.description = detail::bind_strings([field](const Strings& s) { return (s.commands.*field).description; });
    }
    command.execute = std::move(execute);
    command.enabled = std::move(enabled);
    return command;
}

} // namespace ui

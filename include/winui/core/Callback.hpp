// SPDX-License-Identifier: MIT
#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include <cstdint>

namespace ui
{

/// What a handler asked to act returns: whether it dealt with the event, so it goes no further.
/// Written as the constants ui::Handled and ui::NotHandled. A handler that returns nothing gets
/// its event's default: handled where the handler is the target asked to act (a shortcut, a
/// context-menu request), not handled where it watches events pass by (`Window::on_key_preview`).
enum class Handling : std::uint8_t
{
    Handled    = 1, ///< Dealt with; the event goes no further.
    NotHandled = 2, ///< Not this handler's; the event goes on.
};
inline constexpr Handling Handled    = Handling::Handled;    ///< See ui::Handling.
inline constexpr Handling NotHandled = Handling::NotHandled; ///< See ui::Handling.

/// What a handler asked before something happens returns: let it go ahead, or stop it (a window
/// closing, an edit starting or being committed, a sort). Written as the constants ui::Proceed
/// and ui::Cancel. A handler that returns nothing lets it proceed.
enum class Decision : std::uint8_t
{
    Proceed = 1, ///< Go ahead.
    Cancel  = 2, ///< Stop it; nothing changes.
};
inline constexpr Decision Proceed = Decision::Proceed; ///< See ui::Decision.
inline constexpr Decision Cancel  = Decision::Cancel;  ///< See ui::Decision.

namespace detail
{
// An outcome type a handler may leave unreturned; R{} (no enumerator) then means "the default".
template <typename R>
inline constexpr bool is_outcome = std::same_as<R, Handling> || std::same_as<R, Decision>;

// The outcome of a handler call, with `handled_by_default` for a handler that returned nothing
// (and for an empty handler).
[[nodiscard]] constexpr bool handled(Handling outcome, bool handled_by_default) noexcept
{
    return outcome == Handling::Handled || (outcome == Handling{} && handled_by_default);
}
[[nodiscard]] constexpr bool canceled(Decision outcome) noexcept
{
    return outcome == Decision::Cancel;
}
} // namespace detail

/// The one handler type: a `std::move_only_function`, so a handler may capture a
/// `std::unique_ptr`. Spell the signature at the use site (`Callback<void(bool)>`,
/// `Callback<void(std::size_t)>`), so a reader sees the payload without chasing a typedef.
///
/// A handler with a payload also takes a lambda that takes nothing, for when only the event
/// matters: `.on_selection_changed = [&] { refresh(); }`. A handler that returns a ui::Handling or
/// a ui::Decision also takes a lambda that returns nothing, which gets the event's default.
///
/// Example:
/// @code
/// auto dialog = std::make_unique<std::string>("owned state"); // any move-only capture works
/// ui::Callback<void(bool)> on_checked_changed = [owned = std::move(dialog)](bool on) {
///     if (on) { std::puts(owned->c_str()); }
/// };
/// auto toggle = std::make_unique<ui::ToggleSwitchControl>();
/// toggle->on_checked_changed(std::move(on_checked_changed));
/// toggle->on_checked_changed([] { std::puts("toggled"); }); // the payload ignored
/// @endcode
template <typename Sig>
class Callback;

/// See the primary template.
template <typename R, typename... Args>
class Callback<R(Args...)> : public std::move_only_function<R(Args...)>
{
    using Base = std::move_only_function<R(Args...)>;

public:
    using Base::Base;
    using Base::operator=;

    /// Empty.
    Callback() noexcept = default;

    /// A handler returning an outcome, from a callable that returns nothing: the event's default.
    template <typename F>
        requires detail::is_outcome<R> && std::invocable<std::decay_t<F>&, Args...> &&
                 std::is_void_v<std::invoke_result_t<std::decay_t<F>&, Args...>> &&
                 std::move_constructible<std::decay_t<F>>
    Callback(F&& handler) : // NOLINT(google-explicit-constructor)
        Base(
            [inner = std::forward<F>(handler)](Args... args) mutable -> R
            {
                inner(std::forward<Args>(args)...);
                return R{};
            })
    {
    }

    /// The same, from a callable that takes nothing and returns nothing.
    template <typename F>
        requires detail::is_outcome<R> && (sizeof...(Args) > 0) && std::invocable<std::decay_t<F>&> &&
                 (!std::invocable<std::decay_t<F>&, Args...>) &&
                 std::is_void_v<std::invoke_result_t<std::decay_t<F>&>> && std::move_constructible<std::decay_t<F>>
    Callback(F&& handler) : // NOLINT(google-explicit-constructor)
        Base(
            [inner = std::forward<F>(handler)](Args...) mutable -> R
            {
                inner();
                return R{};
            })
    {
    }

    /// A handler with a payload, from a callable that takes nothing (and returns what the
    /// handler returns, if anything).
    template <typename F>
        requires(sizeof...(Args) > 0) && std::invocable<std::decay_t<F>&> &&
                (!std::invocable<std::decay_t<F>&, Args...>) && std::move_constructible<std::decay_t<F>> &&
                (std::is_void_v<R> || std::convertible_to<std::invoke_result_t<std::decay_t<F>&>, R>) &&
                (!(detail::is_outcome<R> && std::is_void_v<std::invoke_result_t<std::decay_t<F>&>>))
    Callback(F&& handler) : // NOLINT(google-explicit-constructor)
        Base(
            [inner = std::forward<F>(handler)](Args...) mutable -> R
            {
                if constexpr (std::is_void_v<R>)
                {
                    inner();
                }
                else
                {
                    return inner();
                }
            })
    {
    }
};

/// See the primary template; a `const`-callable handler, as a const member calls it.
template <typename R, typename... Args>
class Callback<R(Args...) const> : public std::move_only_function<R(Args...) const>
{
    using Base = std::move_only_function<R(Args...) const>;

public:
    using Base::Base;
    using Base::operator=;

    /// Empty.
    Callback() noexcept = default;
};

/// The most common shape, `Callback<void()>`, named because it reads better in a spec field:
/// `.on_click = [&] { save(); }`.
using Action = Callback<void()>;

} // namespace ui

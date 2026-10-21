// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/ThreadCheck.hpp"

#include <array>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

class PropertyBase;
class Element;

// ---------------------------------------------------------------------------------------
// Subscription: one edge of the observer graph, owned by the observer.
// ---------------------------------------------------------------------------------------
/// One edge of the observer graph: keeps a handler registered on a Property or Signal for as
/// long as it lives, owned by the observer.
///
/// Move-only. Destroying, reset()ting or move-assigning over it unsubscribes; if the observed
/// Property dies first the subscription goes dead (alive() turns false), so either end
/// may be destroyed first. Not a template, so an Element holds bindings of any value type in
/// one std::vector<Subscription>.
///
/// Example:
/// @code
/// ui::Property<int> count{0};
/// auto label = std::make_unique<ui::TextControl>("0");
/// ui::TextControl* observer = label.get();
///
/// // Owned by the label: unsubscribes when the label is destroyed.
/// label->add_subscription(count.observe([observer](const int& n) {
///     observer->set_text(std::to_string(n));
/// }));
///
/// // Or held by hand for a scoped observation.
/// ui::Subscription sub = count.observe([](const int& n) { std::printf("%d\n", n); });
/// count = 1;   // both handlers fire
/// sub.reset(); // only the label's remains
/// @endcode
class Subscription
{
public:
    /// A dead subscription observing nothing; alive() is false.
    Subscription() = default;
    /// Unsubscribes (see reset()).
    ~Subscription()
    {
        reset();
    }

    /// Not copyable: one subscription is one edge.
    Subscription(const Subscription&) = delete;
    /// Not copyable: one subscription is one edge.
    Subscription& operator=(const Subscription&) = delete;

    /// Takes over `other`'s place in the observer list; `other` is left dead.
    Subscription(Subscription&& other) noexcept
    {
        splice_from(other);
    }
    /// Unsubscribes this one, then takes over `other`'s place; `other` is left dead.
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            splice_from(other);
        }
        return *this;
    }

    /// Unsubscribe now. Idempotent, and safe after the observed Property has been destroyed.
    void reset() noexcept;

    /// False once this subscription has been reset(), moved from, or had its Property
    /// destroyed out from under it.
    [[nodiscard]] bool alive() const noexcept
    {
        return owner_ != nullptr;
    }

private:
    friend class PropertyBase;
    friend class Element; // unlinks the subscriptions of a subtree it has parked

    void splice_from(Subscription& other) noexcept;
    // Leaves the observer list and goes dead, but keeps the handler alive until reset() or the
    // destructor: the handler may be running further up the stack.
    void unlink() noexcept;

    PropertyBase*               owner_ = nullptr; // the observed Property; nulled if it dies first
    Subscription*               prev_  = nullptr;
    Subscription*               next_  = nullptr;
    Callback<void(const void*)> fn_;
};

// ---------------------------------------------------------------------------------------
// PropertyBase: the list head and the dispatch loop, shared by every Property<T>.
// ---------------------------------------------------------------------------------------
/// The non-template core of Property<T> and Signal: the head of the intrusive observer list and
/// the dispatch loop. Non-copyable; never used on its own and never owned through a pointer to
/// it (the destructor is protected and non-virtual). Its one public member is observer_count().
///
/// Example:
/// @code
/// ui::Property<bool> busy{false};
/// ui::Subscription log = busy.observe([](const bool& on) { std::printf("busy: %d\n", on); });
///
/// // A debug overlay can show how many handlers a piece of state still drives.
/// std::printf("busy has %zu observers\n", busy.observer_count()); // 1
/// log.reset();
/// std::printf("busy has %zu observers\n", busy.observer_count()); // 0
/// @endcode
class PropertyBase
{
public:
    /// Not copyable: subscriptions point at this object.
    PropertyBase(const PropertyBase&) = delete;
    /// Not copyable: subscriptions point at this object.
    PropertyBase& operator=(const PropertyBase&) = delete;

    /// How many live subscriptions currently observe this property. Exposed for
    /// debugging tools; nothing in the framework branches on it.
    [[nodiscard]] std::size_t observer_count() const noexcept
    {
        std::size_t n = 0;
        for (const Subscription* s = head_; s; s = s->next_)
        {
            ++n;
        }
        return n;
    }

protected:
    PropertyBase() = default;

    /// Non-virtual and protected: a Property is never owned or destroyed through a
    /// PropertyBase*, so the type needs no vtable.
    ~PropertyBase()
    {
        for (Subscription* s = head_; s;)
        {
            Subscription* next = s->next_;
            s->owner_          = nullptr;
            s->prev_ = s->next_ = nullptr;
            s                   = next;
        }
        head_ = nullptr;
        // Destroyed from inside one of its own handlers: every dispatch still on the stack ends
        // there, and must not touch this object again.
        for (Cursor* c = cursor_; c; c = c->outer)
        {
            c->next  = nullptr;
            c->owner = nullptr;
        }
    }

    /// Const because subscribing is not a write: it links an edge into the observer list and
    /// changes nothing a reader of the value can see. That is what lets a property whose one
    /// writer is someone else (Window::state(), written only by the window host) be handed out
    /// as `const Property<T>&` and still be bound to.
    ///
    /// `where` names the observe() that called it, for the thread check.
    [[nodiscard]] Subscription attach(Callback<void(const void*)> fn,
                                      std::source_location        where = std::source_location::current()) const
    {
#if LIBWINUI_THREAD_CHECKS
        if (owner_thread_ != std::thread::id{} && owner_thread_ != std::this_thread::get_id())
        {
            detail::report_thread_violation({}, "the property", owner_thread_, where);
        }
#else
        (void)where;
#endif
        Subscription sub;
        sub.fn_    = std::move(fn);
        sub.owner_ = const_cast<PropertyBase*>(this);
        sub.next_  = head_;
        if (head_)
        {
            head_->prev_ = &sub;
        }
        head_ = &sub;
        // Returned by value: with NRVO the caller's object *is* this one and the links are
        // already right; without it, Subscription's move constructor splices them across.
        return sub;
    }

    /// Invoke every observer, newest subscription first. A handler may reset, destroy or move
    /// any Subscription of this source, its own and the next one in line included, and may
    /// destroy the source itself: the observers that are left are still reached, and nobody is
    /// called after the source is gone. A subscription made during the dispatch is first told of
    /// the next change. A handler that resets its own Subscription destroys its own captures by
    /// doing so, and must not touch them afterwards.
    void dispatch(const void* value)
    {
        Cursor cursor{head_, cursor_, this};
        cursor_ = &cursor;
        while (Subscription* s = cursor.next)
        {
            cursor.next = s->next_;
            if (s->fn_)
            {
                s->fn_(value);
            }
        }
    }

    /// The thread check for a write about to be dispatched: the first write that reaches an
    /// observer claims the calling thread, and a later one from any other thread is reported.
    /// A source nobody observes is not checked. `call` names it ("ui::Property::set"); empty
    /// takes the name from `where`.
    void check_write(std::string_view call, const std::source_location& where) const
    {
        if (head_ == nullptr)
        {
            return;
        }
        const std::thread::id self = std::this_thread::get_id();
        if (owner_thread_ == std::thread::id{})
        {
            owner_thread_ = self;
        }
        else if (owner_thread_ != self)
        {
            detail::report_thread_violation(call, "the property", owner_thread_, where);
        }
    }

private:
    friend class Subscription;

    // Where a running dispatch goes next. It lives on the stack of dispatch(), one per nested
    // dispatch, and is patched by whatever unlinks or moves the subscription it points at.
    struct Cursor
    {
        Subscription* next;
        Cursor*       outer;
        PropertyBase* owner; // nulled when the source dies during the dispatch

        Cursor(Subscription* first, Cursor* enclosing, PropertyBase* source) noexcept :
            next(first),
            outer(enclosing),
            owner(source)
        {
        }
        Cursor(const Cursor&)            = delete;
        Cursor& operator=(const Cursor&) = delete;
        ~Cursor()
        {
            if (owner)
            {
                owner->cursor_ = outer;
            }
        }
    };

    mutable Subscription* head_   = nullptr; // mutable: see attach()
    Cursor*               cursor_ = nullptr;
    // Stored with or without LIBWINUI_THREAD_CHECKS, so the layout never depends on it.
    mutable std::thread::id owner_thread_{}; // see check_write(); unset until then
};

inline void Subscription::reset() noexcept
{
    unlink();
    fn_ = {};
}

inline void Subscription::unlink() noexcept
{
    if (!owner_)
    {
        prev_ = next_ = nullptr;
        return;
    }
    for (PropertyBase::Cursor* c = owner_->cursor_; c; c = c->outer)
    {
        if (c->next == this)
        {
            c->next = next_;
        }
    }
    if (prev_)
    {
        prev_->next_ = next_;
    }
    if (next_)
    {
        next_->prev_ = prev_;
    }
    if (owner_->head_ == this)
    {
        owner_->head_ = next_;
    }
    owner_ = nullptr;
    prev_ = next_ = nullptr;
}

inline void Subscription::splice_from(Subscription& other) noexcept
{
    owner_ = other.owner_;
    prev_  = other.prev_;
    next_  = other.next_;
    fn_    = std::move(other.fn_);
    if (prev_)
    {
        prev_->next_ = this;
    }
    if (next_)
    {
        next_->prev_ = this;
    }
    if (owner_ && owner_->head_ == &other)
    {
        owner_->head_ = this;
    }
    if (owner_)
    {
        for (PropertyBase::Cursor* c = owner_->cursor_; c; c = c->outer)
        {
            if (c->next == &other)
            {
                c->next = this;
            }
        }
    }
    other.owner_ = nullptr;
    other.prev_ = other.next_ = nullptr;
}

// ---------------------------------------------------------------------------------------
// Signal: an event with no value, "ask again".
// ---------------------------------------------------------------------------------------
/// An event with no value, meaning "ask again".
///
/// The same edge list as a Property and the same two-way lifetime safety, with no value and so
/// no equality gate: raise() always reaches every observer. A copy (or a move) is a different
/// object with no observers of its own. Assigning to a Signal keeps the observers it has, since
/// they observe this object and not the one it was assigned from. A struct holding one therefore
/// stays an ordinary copyable aggregate.
///
/// Example:
/// @code
/// ui::Signal settings_changed;
/// ui::Subscription reload = settings_changed.observe([&] { std::println("Reloading the settings"); });
///
/// // Every save raises it, and every raise() runs the handler again, even when nothing in the
/// // file changed, since a Signal has no value to compare.
/// window.set_content(ui::Button{.text = "Save", .on_click = [&] { save(); settings_changed.raise(); }});
/// @endcode
class Signal : private PropertyBase
{
public:
    /// A signal with no observers.
    Signal() = default;
    /// Leaves every remaining Subscription dead.
    ~Signal() = default;
    /// A fresh signal with no observers; `other`'s observers are not copied.
    Signal(const Signal&) noexcept : PropertyBase()
    {
    }
    /// No-op: this signal keeps its own observers.
    Signal& operator=(const Signal&) noexcept
    {
        return *this;
    }

    /// Observe. The returned Subscription must be kept for `fn` to keep firing.
    [[nodiscard]] Subscription observe(Callback<void()> fn) const
    {
        return attach([fn = std::move(fn)](const void*) mutable { fn(); });
    }

    /// Tell every observer. Const: raising changes nothing a reader of the signal can see, and the
    /// objects that own one are handed around by const pointer (a control's ui::CommandRef).
    void raise(std::source_location where = std::source_location::current()) const
    {
#if LIBWINUI_THREAD_CHECKS
        check_write("ui::Signal::raise", where);
        const detail::WriteScope scope{where};
#else
        (void)where;
#endif
        const_cast<Signal*>(this)->dispatch(nullptr);
    }

    /// How many live subscriptions observe this signal.
    using PropertyBase::observer_count;
};

namespace detail
{
// Property::operator='s parameter: the value with the place it was written. The conversion to it
// runs a constructor at the caller, so the defaulted location is the caller's line, which an
// operator cannot take as a parameter of its own.
template <typename T>
struct AssignedValue
{
    // `prop = {}`: a value-initialized T.
    AssignedValue(std::source_location at = std::source_location::current()) : value{}, where(at)
    {
    }
    // `prop = x` for anything T is constructible from; U defaults to T so `prop = {x}` works too.
    template <typename U = T>
        requires std::constructible_from<T, U&&>
    AssignedValue(U&& v, std::source_location at = std::source_location::current()) :
        value(std::forward<U>(v)),
        where(at)
    {
    }

    T                    value;
    std::source_location where;
};
} // namespace detail

// ---------------------------------------------------------------------------------------
// Property<T>: an observable value.
// ---------------------------------------------------------------------------------------
/// An observable value: application state written once, from which every display of it is
/// derived through bind() or observe().
///
/// T must be equality comparable, because the equality gate is not optional: set() compares
/// before it assigns, so a no-op write notifies nobody and a dependency cycle terminates instead
/// of recursing. Non-copyable and non-movable, since bindings hold a reference to it. A Property
/// may outlive the controls bound to it or be destroyed first, which ends the bindings. UI thread
/// only: set() is not synchronized.
///
/// Example:
/// @code
/// ui::Property<int> clicks{0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
///         ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
///         ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
///     },
/// });
/// @endcode
template <typename T>
    requires std::equality_comparable<T>
class Property final : public PropertyBase
{
public:
    /// The stored type.
    using value_type = T;

    /// Holds a value-initialized T.
    Property() = default;
    /// Holds `value`; no observer exists yet, so nothing is notified.
    explicit Property(T value) : value_(std::move(value))
    {
    }

    /// The current value. The reference is invalidated by the next set().
    [[nodiscard]] const T& get() const noexcept
    {
        return value_;
    }
    /// Same as get().
    [[nodiscard]] const T& operator*() const noexcept
    {
        return value_;
    }
    /// Member access to the current value, e.g. `title->size()`.
    [[nodiscard]] const T* operator->() const noexcept
    {
        return &value_;
    }

    /// Assign and, if the value changed (`!=` by T's operator==), notify every observer
    /// synchronously before returning. UI thread only: a build with thread checks reports a
    /// write from another thread than the property's first notifying one, naming `where` (the
    /// caller's file and line; leave it defaulted).
    void set(T value, std::source_location where = std::source_location::current())
    {
        if (value == value_)
        {
            return; // the equality gate
        }
#if LIBWINUI_THREAD_CHECKS
        check_write("ui::Property::set", where);
        const detail::WriteScope scope{where};
#else
        (void)where;
#endif
        value_ = std::move(value);
        dispatch(&value_);
    }
    /// Same as set(): `clicks = 0;`, and a thread check report names the caller's line as set()'s
    /// does. A braced value of several parts goes through set(): `size.set({4, 3});`.
    Property& operator=(detail::AssignedValue<T> value)
    {
        set(std::move(value.value), value.where);
        return *this;
    }

    /// Adds `delta` through set(), so the equality gate applies: `count += 5;`,
    /// `log += "Saved\n";`. Exists when `T + T` gives a T, except for bool.
    Property& operator+=(detail::AssignedValue<T> delta)
        requires(!std::same_as<T, bool>) && requires(const T& a, const T& b) {
            { a + b } -> std::convertible_to<T>;
        }
    {
        set(static_cast<T>(value_ + delta.value), delta.where);
        return *this;
    }
    /// Subtracts `delta` through set(): `remaining -= 1;`. Exists when `T - T` gives a T, except
    /// for bool.
    Property& operator-=(detail::AssignedValue<T> delta)
        requires(!std::same_as<T, bool>) && requires(const T& a, const T& b) {
            { a - b } -> std::convertible_to<T>;
        }
    {
        set(static_cast<T>(value_ - delta.value), delta.where);
        return *this;
    }
    /// Adds one through set(): `++clicks;`. Exists when T has a `++`.
    Property& operator++()
        requires requires(T& t) { ++t; }
    {
        T next = value_;
        ++next;
        set(std::move(next));
        return *this;
    }
    /// Subtracts one through set(): `--remaining;`. Exists when T has a `--`.
    Property& operator--()
        requires requires(T& t) { --t; }
    {
        T next = value_;
        --next;
        set(std::move(next));
        return *this;
    }
    /// As `++clicks`, returning the value from before, as `++` on the T itself would.
    T operator++(int)
        requires requires(T& t) { ++t; }
    {
        T old = value_;
        ++*this;
        return old;
    }
    /// As `--remaining`, returning the value from before.
    T operator--(int)
        requires requires(T& t) { --t; }
    {
        T old = value_;
        --*this;
        return old;
    }
    /// Flips a `Property<bool>` through set(): `dark_mode.toggle();`.
    void toggle(std::source_location where = std::source_location::current())
        requires std::same_as<T, bool>
    {
        set(!value_, where);
    }

    /// Read-modify-write for values that are awkward to rebuild by hand (a container, a
    /// struct with several fields). `fn` mutates a copy; the equality gate then decides
    /// whether anything is notified, so a mutation that changes nothing still costs nothing.
    /// `where` is for the thread check, as in set().
    template <typename Fn>
        requires std::invocable<Fn&, T&>
    void update(Fn&& fn, std::source_location where = std::source_location::current())
    {
        T next = value_;
        fn(next);
        set(std::move(next), where);
    }

    /// Observe changes. The returned Subscription must be kept alive for `fn` to keep
    /// firing; store it on the Element that depends on it (Element::add_subscription) so
    /// subscription lifetime is node lifetime. Observers are called newest subscription first.
    /// `fn` is not called for the current value. A caller that wants the initial value reads
    /// get(), as Binding<T> does.
    [[nodiscard]] Subscription observe(Callback<void(const T&)> fn) const
    {
        return attach([handler = std::move(fn)](const void* value) mutable { handler(*static_cast<const T*>(value)); });
    }

private:
    T value_{};
};

template <typename T>
    requires std::equality_comparable<T>
class Computed;

namespace detail
{
// What counts as a source of observe(), the several-source bind() and Computed: a Property or a
// Computed, both read through the Property that holds the value.
template <typename S>
struct SourceOf
{
    static constexpr bool value = false;
};
template <typename T>
struct SourceOf<Property<T>>
{
    static constexpr bool value = true;
    using type                  = T;
};
template <typename T>
struct SourceOf<Computed<T>>
{
    static constexpr bool value = true;
    using type                  = T;
};
template <typename S>
inline constexpr bool is_source = SourceOf<std::remove_cvref_t<S>>::value;
template <typename S>
using source_value_t = typename SourceOf<std::remove_cvref_t<S>>::type;

template <typename T>
[[nodiscard]] const Property<T>& as_property(const Property<T>& source) noexcept
{
    return source;
}
template <typename T>
[[nodiscard]] const Property<T>& as_property(const Computed<T>& source) noexcept
{
    return source.property();
}
} // namespace detail

// ---------------------------------------------------------------------------------------
// observe(fn, sources...): one handler over several sources.
// ---------------------------------------------------------------------------------------
/// Observes several sources with one handler: `fn(a, b, ...)` runs with the latest value of every
/// source whenever any of them changes. Not called for the current values, as Property::observe()
/// is not. The sources are Property or Computed values, in any mix.
///
/// The returned Subscription holds every edge, so keeping it keeps `fn` firing and dropping it
/// ends all of them. It remembers each source's last value, so a source destroyed before the
/// others stops contributing changes.
///
/// Example:
/// @code
/// ui::Property<std::string> first;
/// ui::Property<std::string> last;
/// ui::Subscription greet = ui::observe(
///     [](const std::string& f, const std::string& l) { std::println("Hello, {} {}", f, l); }, first, last);
/// first = "Ada"; // prints "Hello, Ada "
/// @endcode
template <typename Fn, typename... Ss>
    requires(sizeof...(Ss) > 0) && (detail::is_source<Ss> && ...) &&
            std::invocable<Fn&, const detail::source_value_t<Ss>&...> &&
            (std::copy_constructible<detail::source_value_t<Ss>> && ...)
[[nodiscard]] Subscription observe(Fn fn, const Ss&... sources)
{
    using Values = std::tuple<detail::source_value_t<Ss>...>;

    // What the edges share: the handler, the last value of every source, and the edges to all
    // sources but the first. The first edge owns it, so dropping that one drops the rest.
    struct State
    {
        Fn                                          fn;
        Values                                      values;
        std::array<Subscription, sizeof...(Ss) - 1> rest{};
    };

    auto   owned = std::make_unique<State>(State{std::move(fn), Values{detail::as_property(sources).get()...}});
    State* state = owned.get();
    const std::tuple<const Property<detail::source_value_t<Ss>>*...> from{&detail::as_property(sources)...};

    Subscription first;
    const auto   link = [&]<std::size_t I>(std::in_place_index_t<I>)
    {
        using Value        = std::tuple_element_t<I, Values>;
        const auto& source = *std::get<I>(from);
        if constexpr (I == 0)
        {
            first = source.observe(
                [owner = std::move(owned)](const Value& value)
                {
                    std::get<I>(owner->values) = value;
                    std::apply(owner->fn, owner->values);
                });
        }
        else
        {
            state->rest[I - 1] = source.observe(
                [state](const Value& value)
                {
                    std::get<I>(state->values) = value;
                    std::apply(state->fn, state->values);
                });
        }
    };
    [&]<std::size_t... I>(std::index_sequence<I...>)
    { (link(std::in_place_index<I>), ...); }(std::index_sequence_for<Ss...>{});
    return first;
}

/// Deleted: the subscription keeps a reference to each source, so none of them can be a temporary.
template <typename Fn, typename... Sources>
    requires(sizeof...(Sources) > 0) && (detail::is_source<Sources> && ...) &&
                (std::is_rvalue_reference_v<Sources &&> || ...)
Subscription observe(Fn fn, Sources&&... sources) = delete;

// ---------------------------------------------------------------------------------------
// Computed<T>: a value derived from other sources, kept up to date.
// ---------------------------------------------------------------------------------------
/// A read-only value derived from one or more sources: `fn(a, b, ...)`, computed now and again
/// whenever any source changes. The sources are written out where it is declared, in the same
/// shape as the several-source bind(); nothing is tracked behind the scenes.
///
/// It goes through the equality gate like a Property, so its observers and the fields bound to it
/// hear only about results that changed. It binds and observes like a `const Property<T>`
/// (always one-way), and can be a source of another Computed, of bind() and of ui::observe().
///
/// It owns its edges to the sources, and either end may be destroyed first: a source destroyed
/// before it keeps contributing its last value. Not copyable or movable, since the edges point at
/// it. UI-thread only, as Property is.
///
/// Example:
/// @code
/// ui::Property<std::string> first;
/// ui::Property<std::string> last;
/// ui::Property<bool>        agreed{false};
///
/// ui::Computed can_submit{[](const std::string& f, const std::string& l, bool ok) {
///                             return !f.empty() && !l.empty() && ok;
///                         }, first, last, agreed};
///
/// window.set_content(ui::Button{.text = "Submit", .enabled = can_submit});
/// if (can_submit.get()) { save(); }
/// @endcode
template <typename T>
    requires std::equality_comparable<T>
class Computed
{
public:
    /// The stored type.
    using value_type = T;

    /// Computes `fn(sources.get()...)` now, then again whenever a source changes. `fn`'s result
    /// must convert to T.
    template <typename Fn, typename... Ss>
        requires(sizeof...(Ss) > 0) && (detail::is_source<Ss> && ...) &&
                std::invocable<Fn&, const detail::source_value_t<Ss>&...> &&
                std::convertible_to<std::invoke_result_t<Fn&, const detail::source_value_t<Ss>&...>, T> &&
                (std::copy_constructible<detail::source_value_t<Ss>> && ...)
    explicit Computed(Fn fn, const Ss&... sources) : value_(static_cast<T>(fn(detail::as_property(sources).get()...)))
    {
        edges_ = ui::observe([this, fn = std::move(fn)](const detail::source_value_t<Ss>&... values) mutable
                             { value_.set(static_cast<T>(fn(values...))); },
                             sources...);
    }
    /// Deleted: the edges point at this object.
    Computed(const Computed&) = delete;
    /// Deleted: the edges point at this object.
    Computed& operator=(const Computed&) = delete;

    /// The current value. The reference is invalidated by the next change.
    [[nodiscard]] const T& get() const noexcept
    {
        return value_.get();
    }
    /// Same as get().
    [[nodiscard]] const T& operator*() const noexcept
    {
        return value_.get();
    }
    /// Member access to the current value.
    [[nodiscard]] const T* operator->() const noexcept
    {
        return &value_.get();
    }

    /// Observe changes of the result, as Property::observe(): not called for the current value,
    /// and the Subscription must be kept for `fn` to keep firing.
    [[nodiscard]] Subscription observe(Callback<void(const T&)> fn) const
    {
        return value_.observe(std::move(fn));
    }

    /// The read-only Property holding the result, for code that takes a `const Property<T>&`.
    [[nodiscard]] const Property<T>& property() const noexcept
    {
        return value_;
    }

private:
    Property<T>  value_; // declared first: the edges below are dropped before it
    Subscription edges_;
};

/// `ui::Computed c{fn, a, b}` takes its type from what `fn` returns.
template <typename Fn, typename... Ss>
    requires(sizeof...(Ss) > 0) && (detail::is_source<Ss> && ...)
Computed(Fn, const Ss&...)
    -> Computed<std::remove_cvref_t<std::invoke_result_t<Fn&, const detail::source_value_t<Ss>&...>>>;

// ---------------------------------------------------------------------------------------
// Binding<T>: "a value" or "a value that keeps arriving", told apart in the source.
// ---------------------------------------------------------------------------------------
template <typename T>
class ObservableVector;

namespace detail
{
// What a Binding carries besides its value: nothing, except that a binding of a vector can name
// the ObservableVector it came from, for a field that follows single edits (ui::ForEach).
template <typename T>
struct BindingSource
{
    struct NoElement
    {
    };
    using Element  = NoElement;
    using Snapshot = std::nullptr_t; // only a vector's binding names one
};
template <typename U, typename A>
struct BindingSource<std::vector<U, A>>
{
    using Element  = U;
    using Snapshot = void (*)(const ObservableVector<U>&, std::vector<U, A>&);

    ObservableVector<U>* collection = nullptr;
    // Copies the collection into the binding's value the first time initial() is asked, so a field
    // that reads the collection in place never pays for the copy. Cleared once it has run.
    Snapshot snapshot = nullptr;
};

// A binding of a bool made by ui::selects() names the property its choices share, which is what
// groups the controls bound to it.
template <>
struct BindingSource<bool>
{
    struct NoElement
    {
    };
    using Element  = NoElement;
    using Snapshot = std::nullptr_t;

    const void* group = nullptr;
};

template <typename T>
struct OptionalValue
{
    static constexpr bool optional = false;
    using type                     = T; // the type itself, so "the value type" reads the same for both
};
template <typename T>
struct OptionalValue<std::optional<T>>
{
    static constexpr bool optional = true;
    using type                     = T;
};

// Which plain values of another type a Binding<To> takes. Anything convertible, except the
// conversions that lose the value without a word: a pointer, number or enum turned into a bool
// (`.enabled = some_pointer`), a floating-point number into an integer, and an integer into a
// narrower one. An int into a floating-point field stays (`.value = 50`), as does an int into a
// std::size_t of at least its width (`.selected_index = 2`), which Binding asserts is not negative.
template <typename From, typename To>
[[nodiscard]] consteval bool binding_value_converts()
{
    using F = std::remove_cvref_t<From>;
    if constexpr (OptionalValue<To>::optional)
    {
        // An optional field takes what its value type takes (`.checked = some_pointer` neither).
        using V = typename OptionalValue<To>::type;
        if constexpr (std::same_as<F, std::nullopt_t> || (std::convertible_to<F, To> && !std::convertible_to<F, V>))
        {
            return true;
        }
        else
        {
            return binding_value_converts<F, V>();
        }
    }
    else if constexpr (std::same_as<To, bool>)
    {
        return std::same_as<F, bool> || !(std::is_arithmetic_v<F> || std::is_pointer_v<F> || std::is_member_pointer_v<F> ||
                                          std::is_enum_v<F> || std::is_null_pointer_v<F> || std::is_array_v<F>);
    }
    else if constexpr (std::is_integral_v<To> && std::is_arithmetic_v<F>)
    {
        if constexpr (std::is_floating_point_v<F>)
        {
            return false;
        }
        else if constexpr (std::same_as<F, bool> || std::is_signed_v<F> == std::is_signed_v<To>)
        {
            return sizeof(F) <= sizeof(To);
        }
        else if constexpr (std::is_unsigned_v<F>)
        {
            return sizeof(F) < sizeof(To); // into a wider signed type only
        }
        else
        {
            return sizeof(F) <= sizeof(To); // signed into unsigned: asserted non-negative
        }
    }
    else
    {
        return true;
    }
}

// Whether a two-way binding of a source of type To stays two-way in a field of type From: the
// field's values convert back into the source without loss, as by binding_value_converts(). Two
// optionals compare their value types, so a `std::optional<double>` field does not write into a
// `Property<std::optional<int>>`.
template <typename From, typename To>
[[nodiscard]] consteval bool binding_writes_back()
{
    if constexpr (!std::convertible_to<const From&, To>)
    {
        return false;
    }
    else if constexpr (OptionalValue<From>::optional && OptionalValue<To>::optional)
    {
        return binding_value_converts<typename OptionalValue<From>::type, typename OptionalValue<To>::type>();
    }
    else
    {
        return binding_value_converts<From, To>();
    }
}

// A binding of a U that a field of type T shows but cannot write back into: the field takes U's
// values, but its own do not fit a U (a Slider's double into a `Property<int>`). A two-way bind()
// of such a property into an editable field does not compile (TwoWayField).
template <typename U, typename T>
concept LossyWriteBack = (!std::same_as<U, T>) && std::convertible_to<const U&, T> &&
                         (binding_value_converts<U, T>()) && (!binding_writes_back<T, U>());

// The pair TwoWayField refuses for a different reason: a field that can be unset given a
// property that cannot.
template <typename U, typename T>
concept UnsetIntoPlain = OptionalValue<T>::optional && (!OptionalValue<U>::optional);
} // namespace detail

template <typename T>
class Binding;
template <typename T>
class TwoWayBinding;

template <typename T>
[[nodiscard]] Binding<T> bind(const Property<T>& source);
template <typename T>
[[nodiscard]] TwoWayBinding<T> bind(Property<T>& source);
template <typename T>
[[nodiscard]] Binding<std::vector<T>> bind(ObservableVector<T>& source);
template <typename T>
    requires std::copy_constructible<T>
[[nodiscard]] Binding<std::vector<T>> bind(const ObservableVector<T>& source);

namespace detail
{
// An ObservableVector a Binding<std::vector<U>> takes as written (`.items = todos`); a const one
// is copied on every change, so it needs a copyable item.
template <typename S, typename T>
inline constexpr bool is_collection_of = false;
template <typename U>
inline constexpr bool is_collection_of<ObservableVector<U>, std::vector<U>> = true;
template <typename U>
inline constexpr bool is_collection_of<const ObservableVector<U>, std::vector<U>> = std::copy_constructible<U>;

// A Property<U> a Binding<T> takes as written (`.text = name`): its own type, or one whose values
// convert to T without losing anything, as for a plain value.
template <typename U, typename T>
concept PropertyBindsTo = std::same_as<U, T> || (std::convertible_to<const U&, T> && binding_value_converts<U, T>());

// What Binding's source constructor delegates to: bind() of the source, two-way for a writable
// Property.
template <typename S>
[[nodiscard]] auto bind_source(S& source)
{
    if constexpr (!is_source<S>)
    {
        return ui::bind(source); // an ObservableVector
    }
    else if constexpr (std::is_const_v<S> || !std::same_as<S, Property<source_value_t<S>>>)
    {
        return ui::bind(as_property(source));
    }
    else
    {
        return ui::bind(source);
    }
}

// The subscribe thunk of a binding that shows a property verbatim; see Binding::Attach.
template <typename T>
[[nodiscard]] auto attach_property(const Property<T>& source)
{
    return [&source](Callback<void(T)> sink, T* now)
    {
        if (now != nullptr)
        {
            *now = source.get();
        }
        return sink ? source.observe([sink = std::move(sink)](const T& value) mutable { sink(value); }) : Subscription{};
    };
}
} // namespace detail

/// A spec field's value: either a constant or a live binding to a Property, told apart in the
/// source.
///
/// A plain value converts implicitly and is a constant, set once. A Property or a Computed
/// written into the field binds it: `.text = name` follows `name` from then on, two-way on an
/// editable field. bind() is for the rest: a conversion (`ui::bind(clicks, fn)`), several sources,
/// and bind_one_way(). `name.get()` is the current value as a constant. A spec's build() consumes
/// it, subscribing the built control when it is reactive().
///
/// Copyable: one binding can go into several fields, and each control that builds from a copy
/// subscribes on its own, so it follows the property for as long as it lives. A two-way binding
/// copied into several editable fields writes back from each of them, and the equality gate
/// keeps them from writing to each other in turn.
///
/// Example:
/// @code
/// ui::Property<std::string> title{"Untitled"};
/// ui::Property<int>         clicks{0};
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::Text{.text = "static label"}, // a value: set once, never tracked
///     ui::Text{.text = title},          // a binding: tracked
///     ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
/// }});
/// title = "Report.txt"; // the second Text updates
///
/// ui::Binding<std::string> name = ui::bind(title); // one binding, two fields
/// ui::VerticalStack{.content = {ui::TextBox{.text = name}, ui::Text{.text = name}}};
/// @endcode
template <typename T>
class Binding
{
public:
    /// For a binding of a `std::vector`, its element type; an empty placeholder type otherwise.
    using element_type = typename detail::BindingSource<T>::Element;
    /// The reactive form's subscribe thunk: hands a sink to the source property and returns
    /// the resulting Subscription for the built Element to own. When `now` is not null it first
    /// stores the source's value of this moment there; an empty sink only reads.
    using Attach = std::function<Subscription(Callback<void(T)>, T* now)>;
    /// The two-way form's write thunk: stores a value the user entered back into the source.
    using Write = std::function<void(const T&)>;

    /// A constant value-initialized T.
    Binding() = default;

    /// A plain value. Implicit, so `.text = title` works as written.
    Binding(T value) : value_(std::move(value)) // NOLINT(google-explicit-constructor)
    {
    }

    /// A plain value of another type that converts to T, so a field that is a
    /// `Binding<std::optional<Color>>` takes `colors::White` (two user-defined conversions in a
    /// row otherwise, which C++ refuses), a `Binding<float>` takes an int and a
    /// `Binding<std::size_t>` a non-negative int, and a `Binding<std::string>` a string literal. A
    /// conversion that would lose the value does not compile: a pointer or a number into a bool
    /// field, a floating-point number into an integer one, an integer into a narrower one.
    template <typename U>
        requires(!std::same_as<std::remove_cvref_t<U>, T> && !std::same_as<std::remove_cvref_t<U>, Binding> &&
                 std::convertible_to<U, T>)
    Binding(U&& value) : value_(convert(std::forward<U>(value))) // NOLINT(google-explicit-constructor)
    {
        // A static_assert, not a constraint, so a lossy conversion still picks this constructor over
        // Binding(T) and fails here. A constraint needs a second, deleted constructor with the
        // opposite one, and MSVC checks both for every value written into a spec field, which costs
        // about a third of the compiler memory a page of specs takes.
        static_assert(detail::binding_value_converts<U, T>(),
                      "this value would lose information in the field's type (a number or pointer into a bool, a "
                      "floating-point number into an integer, an integer into a narrower one)");
        // Also inside an optional field: `.selected_index = -1` is a mistake, not "nothing".
        using V = typename detail::OptionalValue<T>::type;
        if constexpr (std::is_integral_v<std::remove_cvref_t<U>> && std::is_signed_v<std::remove_cvref_t<U>> &&
                      std::is_unsigned_v<V> && !std::same_as<V, bool>)
        {
            assert(value >= 0 && "a negative number for an unsigned field");
        }
    }

    /// A value built in place from a braced list of its constructor's arguments, so a field takes
    /// `{a, b}` as a plain field of its value type would, and an optional field as its value type
    /// would.
    template <typename A, typename B, typename... Rest>
        requires(!std::same_as<std::remove_cvref_t<B>, Attach>) &&
                (std::constructible_from<T, A, B, Rest...> ||
                 (detail::OptionalValue<T>::optional &&
                  std::constructible_from<typename detail::OptionalValue<T>::type, A, B, Rest...>))
    Binding(A&& a, B&& b, Rest&&... rest) : // NOLINT(google-explicit-constructor)
        value_(make_in_place(std::forward<A>(a), std::forward<B>(b), std::forward<Rest>(rest)...))
    {
    }

    /// For a binding of a vector: its elements as a braced list, so `.items = {"Inbox", "Sent"}`
    /// needs no `std::vector<std::string>` spelled out.
    Binding(std::initializer_list<element_type> elements) // NOLINT(google-explicit-constructor)
        requires requires(const detail::BindingSource<T>& source) { source.collection; }
        : value_(elements)
    {
    }

    /// The reactive form. bind() builds it; callers never spell it out.
    Binding(T initial, Attach attach) : value_(std::move(initial)), attach_(std::move(attach))
    {
    }
    /// A two-way binding: reactive, and able to write a new value back into its source. What
    /// bind() makes from a non-const Property.
    Binding(T initial, Attach attach, Write write) :
        value_(std::move(initial)),
        attach_(std::move(attach)),
        write_(std::move(write))
    {
    }

    /// A two-way binding of a bool that also names the group of choices it is one of; what
    /// ui::selects() makes.
    Binding(T initial, Attach attach, Write write, const void* group)
        requires requires(const detail::BindingSource<T>& source) { source.group; }
        : value_(std::move(initial)), attach_(std::move(attach)), write_(std::move(write))
    {
        source_.group = group;
    }

    /// A binding of a vector that also names the collection it came from; what bind() of a
    /// ui::ObservableVector makes. `snapshot` copies the collection into initial() the first time
    /// it is asked; null leaves it empty (an item type that cannot be copied).
    Binding(ObservableVector<element_type>* collection, typename detail::BindingSource<T>::Snapshot snapshot, Attach attach)
        requires requires(const detail::BindingSource<T>& source) { source.collection; }
        : attach_(std::move(attach))
    {
        source_.collection = collection;
        source_.snapshot   = snapshot;
    }

    /// A Property, a Computed or an ObservableVector, bound: `.text = name` is the same as
    /// `.text = ui::bind(name)`, `.items = todos` as `.items = ui::bind(todos)`. A writable
    /// Property is two-way on an editable field, as bind() is; a `const` Property and a Computed
    /// are one-way. A source of another type converts as a bind() of it would.
    template <typename S>
        requires(detail::is_source<S> && detail::PropertyBindsTo<detail::source_value_t<S>, T>) ||
                detail::is_collection_of<S, T>
    Binding(S& source) : Binding(detail::bind_source(source)) // NOLINT(google-explicit-constructor)
    {
    }

    /// A binding of another type that converts to T, still reactive: a `Binding<std::string>`
    /// takes a bind() whose function returns a string literal, a `Binding<double>` a
    /// `Property<int>`, a `Binding<std::optional<bool>>` a `Property<bool>`. The conversions a
    /// plain value refuses (a number into a bool, a floating-point number into an integer, an
    /// integer into a narrower one) are refused here too.
    ///
    /// A two-way binding stays two-way when the value converts back without loss, as between
    /// float and double: a Slider's `value` takes `ui::bind()` of a `Property<float>`, and what
    /// the user drags is written back as a float. Otherwise the result is one-way, and an
    /// editable field (TwoWayField) refuses it at compile time.
    template <typename U>
        requires(!std::same_as<U, T>) && std::convertible_to<const U&, T> && (detail::binding_value_converts<U, T>())
    Binding(Binding<U> other) : value_(convert(other.initial())) // NOLINT(google-explicit-constructor)
    {
        if (other.attach_)
        {
            attach_ = [inner = std::move(other.attach_)](Callback<void(T)> sink, T* now) mutable
            {
                U            value{};
                Subscription subscription = inner(sink ? Callback<void(U)>{[sink = std::move(sink)](U v) mutable
                                                                           { sink(convert(std::move(v))); }}
                                                       : Callback<void(U)>{},
                                                  now != nullptr ? &value : nullptr);
                if (now != nullptr)
                {
                    *now = convert(std::move(value));
                }
                return subscription;
            };
        }
        if constexpr (detail::binding_writes_back<T, U>())
        {
            if (other.write_)
            {
                write_ = [inner = std::move(other.write_)](const T& value) mutable { inner(static_cast<U>(value)); };
            }
        }
    }

    /// A copy subscribes on its own when its field is built; see the class comment.
    Binding(const Binding&) = default;
    /// Same as the copy constructor.
    Binding& operator=(const Binding&) = default;
    /// Takes `other`'s value and thunks; `other` is left a constant.
    Binding(Binding&& other) noexcept(std::is_nothrow_move_constructible_v<T>) :
        value_(std::move(other.value_)),
        attach_(std::exchange(other.attach_, nullptr)),
        write_(std::exchange(other.write_, nullptr)),
        source_(other.source_)
    {
    }
    /// Same as the move constructor.
    Binding& operator=(Binding&& other) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value_  = std::move(other.value_);
        attach_ = std::exchange(other.attach_, nullptr);
        write_  = std::exchange(other.write_, nullptr);
        source_ = other.source_;
        return *this;
    }

    /// The value as of construction: what a control is initialized with, whether or not
    /// this binding is reactive. For a binding of an ObservableVector, a copy of the collection
    /// made the first time it is asked, so a field that reads the collection in place
    /// (observable()) never makes one.
    [[nodiscard]] const T& initial() const
    {
        if constexpr (requires { source_.snapshot; })
        {
            if (source_.snapshot != nullptr)
            {
                source_.snapshot(*source_.collection, value_);
                source_.snapshot = nullptr;
            }
        }
        return value_;
    }
    /// The value the source holds right now: what a control that subscribes later than the
    /// binding was made starts from, since a property may have changed in between. initial() for a
    /// constant.
    [[nodiscard]] T current() const
    {
        if (!attach_)
        {
            return initial();
        }
        T now{};
        (void)attach_(Callback<void(T)>{}, &now);
        return now;
    }
    /// True if this came from bind() and will keep delivering values; false for a constant.
    [[nodiscard]] bool reactive() const noexcept
    {
        return static_cast<bool>(attach_);
    }

    /// Start tracking. Only valid when reactive(); the returned Subscription must outlive
    /// the interest in `sink`. Callers use ui::apply() (ui/spec/Common.hpp) instead, which also
    /// sets the initial value.
    [[nodiscard]] Subscription subscribe(Callback<void(T)> sink)
    {
        assert(attach_ && "Binding::subscribe() needs a reactive() binding");
        return attach_ ? attach_(std::move(sink), nullptr) : Subscription{};
    }
    /// True when the binding can write back into its source: bind() of a non-const Property with
    /// no conversion, or one turned into a field type it converts back from without loss.
    [[nodiscard]] bool writable() const noexcept
    {
        return static_cast<bool>(write_);
    }
    /// The write thunk, moved out for the control that will call it; empty for a one-way
    /// binding. An editable field's build() takes it (ui::write_back()); a display field never
    /// does, which is what makes the same bind() one-way there.
    [[nodiscard]] Write take_writer() noexcept
    {
        return std::move(write_);
    }
    /// For a binding of a vector: the ui::ObservableVector it was made from, or nullptr when it
    /// came from a plain vector or a Property. A field that can follow single edits reads the
    /// collection through this instead of taking whole copies from subscribe().
    [[nodiscard]] auto* observable() const noexcept
        requires requires(const detail::BindingSource<T>& source) { source.collection; }
    {
        return source_.collection;
    }
    /// For a binding of a bool made by ui::selects(): the property whose choices it is one of, the
    /// same for every binding of that property. Controls bound with the same group form one set of
    /// choices (RadioButton). nullptr for any other binding.
    [[nodiscard]] const void* selection_group() const noexcept
        requires requires(const detail::BindingSource<T>& source) { source.group; }
    {
        return source_.group;
    }

private:
    template <typename>
    friend class Binding;

    // A value of another type, as T. Into an optional field the value converts to the value type
    // first, so `.width = 100` is the same int-to-float conversion as in a plain float field. An
    // integer into a type built from a float (`.margin = 24`, a Thickness) is made a float here,
    // where the value is known to fit, rather than inside that type's constructor.
    template <typename U>
    [[nodiscard]] static T convert(U&& value)
    {
        using V = typename detail::OptionalValue<T>::type;
        if constexpr (std::integral<std::remove_cvref_t<U>> && !std::is_arithmetic_v<V> && std::constructible_from<V, float>)
        {
            return T(V(static_cast<float>(value)));
        }
        else if constexpr (detail::OptionalValue<T>::optional &&
                           !std::same_as<std::remove_cvref_t<U>, std::nullopt_t> && std::convertible_to<U, V>)
        {
            return T(static_cast<V>(std::forward<U>(value)));
        }
        else
        {
            return static_cast<T>(std::forward<U>(value));
        }
    }

    // `{4, 8}` hands ints to a constructor taking floats, as a plain aggregate field would take
    // them without a word.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
    template <typename... Args>
    [[nodiscard]] static T make_in_place(Args&&... args)
    {
        if constexpr (std::constructible_from<T, Args...>)
        {
            return T(std::forward<Args>(args)...);
        }
        else
        {
            return T(typename detail::OptionalValue<T>::type(std::forward<Args>(args)...));
        }
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    // Mutable for the copy initial() makes of an ObservableVector on first use; nothing else
    // changes them through a const Binding.
    mutable T                                                   value_{};
    Attach                                                      attach_{};
    Write                                                       write_{};
    LIBWINUI_NO_UNIQUE_ADDRESS mutable detail::BindingSource<T> source_{};
};

/// What bind() of a non-const Property returns: a Binding<T> that writes back. It converts to
/// every field a `Binding<T>` does, except an editable field that can be unset (a TwoWayField
/// of `std::optional<T>`). Nothing names it; a binding kept in a variable is `auto` or a
/// `Binding<T>`.
template <typename T>
class TwoWayBinding : public Binding<T>
{
public:
    using Binding<T>::Binding;
};

/// What bind_choice() returns: a two-way binding of an index onto a property holding one of a
/// list of values. It goes into any index field, one that can show no selection (a ListView's
/// `selected_index`) and one that always shows one (a Sections control's). Nothing names it; see
/// bind_choice().
class ChoiceBinding : public Binding<std::optional<std::size_t>>
{
public:
    using Binding<std::optional<std::size_t>>::Binding;
};

/// The type of an editable spec field: one the user changes and a two-way bind() writes back
/// from (a text box's `text`, a slider's `value`, a `selected_index`, an `is_open`). It is a
/// Binding<T> in every way but two, both refusing a two-way bind() of a property that cannot
/// hold what the control writes back, where a plain Binding would silently go one-way:
///
/// - A writable `Property<U>` whose type loses some of the field's values does not compile:
///   `.value = level` with a `Property<int> level` on a Slider, whose value is a double. Bind a
///   property of the field's own type, or say one-way with bind_one_way(). Types that convert
///   both ways without loss stay two-way (a `Property<float>` on that same Slider).
/// - A field that can be unset (a selection that can be empty, a check box's indeterminate
///   state, an empty number box) is a `TwoWayField<std::optional<T>>`, and a two-way bind() of
///   a `Property<T>` into it does not compile: the control can go unset, which the property
///   cannot hold. Bind a `Property<std::optional<T>>`, or bind_one_way().
///
/// A display field stays a plain Binding, and bind() of any property that converts goes into it
/// one-way.
///
/// Example:
/// @code
/// ui::Property<std::optional<std::size_t>> picked;
/// ui::Property<std::size_t>                size{0};
/// ui::Property<int>                        volume{40};
/// ui::ComboBox{.items = {"Small", "Large"}, .selected_index = picked};       // two-way
/// ui::ComboBox{.items = {"Small", "Large"}, .selected_index = ui::bind_one_way(size)}; // one-way
/// ui::Slider{.value = ui::bind_one_way(volume)};                             // one-way
/// // .selected_index = size does not compile: the dropdown can be left with
/// // nothing selected, which `size` cannot hold.
/// // .value = volume does not compile: a drag to 40.5 does not fit an int.
/// @endcode
template <typename T>
class TwoWayField : public Binding<T>
{
public:
    using Binding<T>::Binding;

    /// A constant value-initialized T.
    TwoWayField() = default;
    /// Any binding of the field's own type: a constant, bind() or bind_one_way().
    TwoWayField(Binding<T> binding) : Binding<T>(std::move(binding)) // NOLINT(google-explicit-constructor)
    {
    }
    /// bind_choice() into an index field that always shows a selection. A value not in the list
    /// leaves the selection where it is, and starts the field on the first choice.
    TwoWayField(ChoiceBinding choice) // NOLINT(google-explicit-constructor)
        requires std::same_as<T, std::size_t>
        :
        Binding<T>(
            choice.initial().value_or(0),
            [inner = Binding<std::optional<std::size_t>>(choice)](Callback<void(std::size_t)> sink, std::size_t* now) mutable
            {
                if (now != nullptr)
                {
                    *now = inner.current().value_or(0);
                }
                return sink ? inner.subscribe(
                                  [sink = std::move(sink)](std::optional<std::size_t> index) mutable
                                  {
                                      if (index.has_value())
                                      {
                                          sink(*index);
                                      }
                                  })
                            : Subscription{};
            },
            [write = choice.take_writer()](const std::size_t& index)
            {
                if (write)
                {
                    write(index);
                }
            })
    {
    }
    /// Does not compile: a two-way bind() of a `Property<U>` that cannot hold every value of the
    /// field. See the class comment.
    template <typename U>
        requires detail::LossyWriteBack<U, T> && (!detail::UnsetIntoPlain<U, T>)
    TwoWayField(const TwoWayBinding<U>& two_way) // NOLINT(google-explicit-constructor)
    {
        static_assert(detail::binding_writes_back<T, U>(),
                      "this editable field writes values back that the bound Property cannot hold without loss: bind "
                      "a Property of the field's own type, or use ui::bind_one_way() to only show the property");
        (void)two_way;
    }
    /// Does not compile: a writable `Property<U>` written straight into the field, the same
    /// two-way bind() as above. See the class comment.
    template <typename S>
        requires detail::is_source<S> && (!std::is_const_v<S>) && std::same_as<S, Property<detail::source_value_t<S>>> &&
                 detail::LossyWriteBack<detail::source_value_t<S>, T> &&
                 (!detail::UnsetIntoPlain<detail::source_value_t<S>, T>)
    TwoWayField(S& source) // NOLINT(google-explicit-constructor)
    {
        static_assert(detail::binding_writes_back<T, detail::source_value_t<S>>(),
                      "this editable field writes values back that the bound Property cannot hold without loss: bind "
                      "a Property of the field's own type, or use ui::bind_one_way() to only show the property");
        (void)source;
    }
    /// Deleted: a two-way bind() of a `Property<U>` into a field that can be unset. See the class
    /// comment.
    template <typename U>
        requires detail::OptionalValue<T>::optional && (!detail::OptionalValue<U>::optional)
    TwoWayField(const TwoWayBinding<U>& two_way) = delete;
    /// Deleted: a writable `Property<U>` written straight into a field that can be unset, which is
    /// the same two-way bind() as above. See the class comment.
    template <typename S>
        requires detail::is_source<S> && detail::PropertyBindsTo<detail::source_value_t<S>, T> &&
                     (!std::is_const_v<S>) && std::same_as<S, Property<detail::source_value_t<S>>> &&
                     detail::OptionalValue<T>::optional && (!detail::OptionalValue<detail::source_value_t<S>>::optional)
    TwoWayField(S& source) = delete;
};

/// The type of a check box's `checked` (CheckBox, ToggleButton, AppBarToggleButton): true, false
/// or std::nullopt for indeterminate. It is a `Binding<std::optional<bool>>` that also takes a
/// writable `Property<bool>` two-way, since a click only ever produces true or false: the
/// property gets each click, and an indeterminate state the app sets on the control writes
/// nothing. A `Property<std::optional<bool>>` is two-way as everywhere else, and is the one to
/// bind when a click can reach indeterminate (the spec's `three_state`).
///
/// Example:
/// @code
/// ui::Property<bool>                agreed{false};
/// ui::Property<std::optional<bool>> all{false}; // std::nullopt while only some items are on
/// ui::CheckBox{.text = "I agree", .checked = agreed};     // two-way, two states
/// ui::CheckBox{.text = "Select all", .checked = all};     // two-way, shows mixed
/// @endcode
class CheckedField : public Binding<std::optional<bool>>
{
public:
    using Binding<std::optional<bool>>::Binding;

    /// Unchecked.
    CheckedField() = default;
    /// Any binding of the field's own type: a constant, bind() or bind_one_way().
    CheckedField(Binding<std::optional<bool>> binding) : // NOLINT(google-explicit-constructor)
        Binding<std::optional<bool>>(std::move(binding))
    {
    }
    /// A writable `Property<bool>`, two-way: the same as bind() of it.
    CheckedField(Property<bool>& source) : CheckedField(ui::bind(source)) // NOLINT(google-explicit-constructor)
    {
    }
    /// A two-way binding of a bool (bind() of a `Property<bool>`, ui::selects()): the control
    /// follows it, and a click writes true or false back. An indeterminate state is never written.
    CheckedField(TwoWayBinding<bool> two_way) : // NOLINT(google-explicit-constructor)
        Binding<std::optional<bool>>(
            std::optional<bool>{two_way.initial()},
            [inner = Binding<bool>(two_way)](Callback<void(std::optional<bool>)> sink, std::optional<bool>* now) mutable
            {
                if (now != nullptr)
                {
                    *now = inner.current();
                }
                return sink ? inner.subscribe([sink = std::move(sink)](bool value) mutable { sink(value); })
                            : Subscription{};
            },
            [write = two_way.take_writer()](const std::optional<bool>& value)
            {
                if (value.has_value())
                {
                    write(*value);
                }
            }),
        writes_bool_(true),
        group_(two_way.selection_group())
    {
    }

    /// True when a click writes into a `Property<bool>`, which cannot hold indeterminate.
    [[nodiscard]] bool writes_bool() const noexcept
    {
        return writes_bool_;
    }
    /// The group of choices it came from, for a binding made by ui::selects(); nullptr otherwise.
    [[nodiscard]] const void* selection_group() const noexcept
    {
        return group_;
    }

private:
    bool        writes_bool_ = false;
    const void* group_       = nullptr;
};

/// One-source binding: the property's value, verbatim.
///
/// Takes the property by const reference, since observing is not writing. A read-only property a
/// host owns binds like one the app owns. The rvalue overloads are deleted because a binding keeps
/// a reference to its source, and a temporary Property would be gone before the first change.
///
/// Example:
/// @code
/// ui::Property<bool> online{true};
/// window.set_content(ui::ToggleSwitch{.checked = ui::bind(online)}); // or `.checked = online`
/// @endcode
template <typename T>
[[nodiscard]] Binding<T> bind(const Property<T>& source)
{
    return Binding<T>{source.get(), detail::attach_property(source)};
}

/// One-source binding with a transform, so a format string exists once in a program instead of
/// once per writer. The result type is whatever `fn` returns. `fn` runs once now for the initial
/// value and again on every change. It is one-way; bind(source, to, from) adds the way back.
///
/// Example:
/// @code
/// ui::Property<int> clicks{0};
/// ui::Text label{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })};
/// @endcode
///
/// `fn` must be copyable, because it is stored once per subscription edge.
template <typename T, typename Fn>
    requires std::invocable<Fn&, const T&> && std::copy_constructible<Fn>
[[nodiscard]] auto bind(const Property<T>& source, Fn fn)
{
    using Result = std::remove_cvref_t<std::invoke_result_t<Fn&, const T&>>;
    return Binding<Result>{fn(source.get()),
                           [&source, fn](Callback<void(Result)> sink, Result* now) mutable
                           {
                               if (now != nullptr)
                               {
                                   *now = fn(source.get());
                               }
                               return sink ? source.observe([sink = std::move(sink), fn](const T& value) mutable
                                                            { sink(fn(value)); })
                                           : Subscription{};
                           }};
}

/// One-way binding of a property, whatever field it lands on: the control shows the property and
/// never writes to it. The way to opt an editable field out of bind()'s two-way default, for an
/// input that only displays state or whose edits the app validates before storing.
///
/// Example:
/// @code
/// ui::Property<float> level{0.4f};
/// // The slider follows `level`; dragging it does not change `level` by itself.
/// // The app stores only the levels it accepts.
/// window.set_content(ui::Slider{
///     .value            = ui::bind_one_way(level),
///     .on_value_changed = [&](double v) { if (v <= 0.8) { level = static_cast<float>(v); } },
/// });
/// @endcode
template <typename T>
[[nodiscard]] Binding<T> bind_one_way(const Property<T>& source)
{
    return bind(source);
}
/// Deleted: a binding keeps a reference to its source, so a temporary Property cannot be bound.
template <typename T>
Binding<T> bind_one_way(const Property<T>&& source) = delete;

/// Binding of a property the app can write, with no conversion. On an editable field (a text
/// box's `.text`, a slider's `.value`, a toggle's `.checked`, a list's `.selected_index`, ...) it is
/// two-way: the control shows the property, and what the user enters is written back into it,
/// before the field's own `on_*_changed` handler runs. On a display field it is one-way, as there
/// is nothing to write. A `const` Property and a bind() with a conversion are always one-way;
/// bind_one_way() opts an editable field out. An editable field that can be unset takes it only
/// from a `Property<std::optional<T>>`; see TwoWayField.
///
/// The equality gate of Property::set() ends the round trip: the value written back equals the
/// one the control already shows.
///
/// Example:
/// @code
/// ui::Property<std::string> name{"Ada"};
/// window.set_content(ui::VerticalStack{.content = {
///     ui::TextBox{.text = ui::bind(name)},                                   // typing changes `name`
///     ui::Text{.text = ui::bind(name, [](const std::string& n) { return "Hello, " + n; })},
/// }});
/// @endcode
///
/// This overload also keeps ADL from picking std::bind for a non-const lvalue of a std:: type.
template <typename T>
[[nodiscard]] TwoWayBinding<T> bind(Property<T>& source)
{
    return TwoWayBinding<T>{source.get(),
                            detail::attach_property(source),
                            [&source](const T& value) { source.set(value); }};
}
/// One choice of several, written into a control's `checked`: the control is checked while
/// `source` holds `value`, and checking it sets `source` to `value`. Unchecking writes nothing,
/// since a choice is only ever left by making another one.
///
/// Every control bound through selects() to the same property is one set of choices, wherever it
/// sits in the tree: checking one unchecks the others through the property. RadioButtons bound
/// this way also act as one group for the keyboard and screen readers: Tab stops once on the group
/// (on the checked button, or the first), the arrow keys move between the buttons and check the
/// one they reach (Ctrl+arrow moves without checking), and each button reports its position in
/// the set. A ToggleButton bound this way stays checked when clicked again, like a radio button.
/// For one control that lists the choices itself (a SelectorBar, RadioButtons), see bind_choice().
///
/// Example:
/// @code
/// enum class Size { Small, Medium, Large };
/// ui::Property<Size> size{Size::Medium};
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::RadioButton{.text = "Small", .checked = ui::selects(size, Size::Small)},
///     ui::Text{.text = "Fits a phone."},
///     ui::RadioButton{.text = "Medium", .checked = ui::selects(size, Size::Medium)},
///     ui::RadioButton{.text = "Large", .checked = ui::selects(size, Size::Large)},
/// }});
/// @endcode
template <typename T, typename V>
    requires std::constructible_from<T, V> && std::equality_comparable<T>
[[nodiscard]] TwoWayBinding<bool> selects(Property<T>& source, V&& value)
{
    T choice(std::forward<V>(value));
    return TwoWayBinding<bool>{source.get() == choice,
                               [&source, choice](Callback<void(bool)> sink, bool* now)
                               {
                                   if (now != nullptr)
                                   {
                                       *now = source.get() == choice;
                                   }
                                   return sink ? source.observe([sink = std::move(sink), choice](const T& value) mutable
                                                                { sink(value == choice); })
                                               : Subscription{};
                               },
                               [&source, choice](const bool& on)
                               {
                                   if (on)
                                   {
                                       source.set(choice);
                                   }
                               },
                               static_cast<const void*>(&source)};
}
/// Deleted: the binding keeps a reference to its source, so a temporary Property cannot be used.
template <typename T, typename V>
TwoWayBinding<bool> selects(Property<T>&& source, V&& value) = delete;

/// A property holding one of several values, bound two-way to an index field
/// (`selected_index` on a SelectorBar, Sections, Carousel, PipsPager, ComboBox,
/// RadioButtons, ListView, ...). `choices` lists the values in the order of the control's items:
/// the field shows the position of the value `source` holds, and picking item `i` sets `source`
/// to `choices[i]`.
///
/// A value that is not in the list shows no selection where the field can show none (a
/// ComboBox, a ListView). A field that always shows a selection (a Sections control, a SelectorBar) keeps the
/// one it has, and starts on the first choice. Clearing the selection writes nothing.
///
/// For a choice made with separate controls, one per value, see selects().
///
/// Example:
/// @code
/// enum class Filter { All, Active, Done };
/// ui::Property<Filter> filter{Filter::All};
///
/// ui::SelectorBar{
///     .items          = {"All", "Active", "Done"},
///     .selected_index = ui::bind_choice(filter, {Filter::All, Filter::Active, Filter::Done}),
/// };
/// filter = Filter::Done; // selects "Done"
/// @endcode
template <typename T>
    requires std::equality_comparable<T> && std::copy_constructible<T>
[[nodiscard]] ChoiceBinding bind_choice(Property<T>& source, std::vector<T> choices)
{
    const auto list     = std::make_shared<const std::vector<T>>(std::move(choices));
    const auto position = [list](const T& value) -> std::optional<std::size_t>
    {
        for (std::size_t i = 0; i < list->size(); ++i)
        {
            if ((*list)[i] == value)
            {
                return i;
            }
        }
        return std::nullopt;
    };
    return ChoiceBinding{position(source.get()),
                         [&source, position](Callback<void(std::optional<std::size_t>)> sink, std::optional<std::size_t>* now)
                         {
                             if (now != nullptr)
                             {
                                 *now = position(source.get());
                             }
                             return sink ? source.observe([sink = std::move(sink), position](const T& value) mutable
                                                          { sink(position(value)); })
                                         : Subscription{};
                         },
                         [&source, list](const std::optional<std::size_t>& index)
                         {
                             if (index.has_value() && *index < list->size())
                             {
                                 source.set((*list)[*index]);
                             }
                         }};
}
/// Deleted: the binding keeps a reference to its source, so a temporary Property cannot be used.
template <typename T>
ChoiceBinding bind_choice(Property<T>&& source, std::vector<T> choices) = delete;

/// As bind_choice() with a fixed list, for a list that changes: the field shows the position of
/// `source`'s value in `choices` as it is now, and follows either one. Reordering the list moves
/// the selection with the value. A list that no longer holds the value shows no selection, and
/// `source` keeps its value until the next pick.
///
/// Example:
/// @code
/// ui::Property<std::vector<std::string>> names{{"Ada", "Grace"}};
/// ui::Property<std::string>              chosen{"Grace"};
/// window.set_content(ui::ComboBox{
///     .items          = ui::bind(names),
///     .selected_index = ui::bind_choice(chosen, names),
/// });
/// names = std::vector<std::string>{"Grace", "Ada", "Hedy"}; // the selection moves with "Grace"
/// @endcode
template <typename T>
    requires std::equality_comparable<T> && std::copy_constructible<T>
[[nodiscard]] ChoiceBinding bind_choice(Property<T>& source, const Property<std::vector<T>>& choices)
{
    const auto position = [](const T& value, const std::vector<T>& list) -> std::optional<std::size_t>
    {
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            if (list[i] == value)
            {
                return i;
            }
        }
        return std::nullopt;
    };
    return ChoiceBinding{position(source.get(), choices.get()),
                         [&source, &choices, position](Callback<void(std::optional<std::size_t>)> sink,
                                                       std::optional<std::size_t>*                now)
                         {
                             if (now != nullptr)
                             {
                                 *now = position(source.get(), choices.get());
                             }
                             if (!sink)
                             {
                                 return Subscription{};
                             }
                             return ui::observe([sink = std::move(sink),
                                                 position](const T& value, const std::vector<T>& list) mutable
                                                { sink(position(value, list)); },
                                                source,
                                                choices);
                         },
                         [&source, &choices](const std::optional<std::size_t>& index)
                         {
                             const std::vector<T>& list = choices.get();
                             if (index.has_value() && *index < list.size())
                             {
                                 source.set(list[*index]);
                             }
                         }};
}
/// Deleted: the binding keeps references to both properties, so neither can be a temporary.
template <typename T>
ChoiceBinding bind_choice(Property<T>&& source, const Property<std::vector<T>>& choices) = delete;
/// Deleted: the binding keeps references to both properties, so neither can be a temporary.
template <typename T>
ChoiceBinding bind_choice(Property<T>& source, const Property<std::vector<T>>&& choices) = delete;

/// Same as bind(const Property<T>&, Fn); exists so ADL never picks std::bind.
template <typename T, typename Fn>
    requires std::invocable<Fn&, const T&> && std::copy_constructible<Fn>
[[nodiscard]] auto bind(Property<T>& source, Fn fn)
{
    return bind(static_cast<const Property<T>&>(source), std::move(fn));
}

/// Two-way binding through a conversion: `to` turns the property's value into the field's, and
/// `from` turns what the user enters back into the property's type. For a field whose type isn't
/// the model's own, such as a ComboBox index standing for a bool, so the model keeps its type.
///
/// The property is written through its equality gate, so a `from` that gives back the value it
/// already holds changes nothing downstream. `to` runs once now and on every change; `from` on
/// every write back. Both must be copyable, as for the one-source form.
///
/// Example:
/// @code
/// ui::Property<bool> any{false};
/// window.set_content(ui::ComboBox{
///     .items          = {"All of these", "Any of these"},
///     .selected_index = ui::bind(
///         any,
///         [](bool a) { return std::optional<std::size_t>{a ? std::size_t{1} : std::size_t{0}}; },
///         [](const std::optional<std::size_t>& index) { return index == std::size_t{1}; }),
/// });
/// @endcode
///
/// Three arguments with a Property first: a one-source bind() never considers it, and a
/// several-source bind() of a function rejects it at once, so it costs other calls nothing.
template <typename T, typename To, typename From>
    requires std::invocable<To&, const T&> && std::copy_constructible<To> && std::copy_constructible<From> &&
             std::invocable<From&, const std::remove_cvref_t<std::invoke_result_t<To&, const T&>>&> &&
             std::convertible_to<std::invoke_result_t<From&, const std::remove_cvref_t<std::invoke_result_t<To&, const T&>>&>, T>
[[nodiscard]] auto bind(Property<T>& source, To to, From from)
{
    using Result = std::remove_cvref_t<std::invoke_result_t<To&, const T&>>;
    return TwoWayBinding<Result>{to(source.get()),
                                 [&source, to](Callback<void(Result)> sink, Result* now) mutable
                                 {
                                     if (now != nullptr)
                                     {
                                         *now = to(source.get());
                                     }
                                     return sink ? source.observe([sink = std::move(sink), to](const T& value) mutable
                                                                  { sink(to(value)); })
                                                 : Subscription{};
                                 },
                                 [&source, from](const Result& value) mutable { source.set(T(from(value))); }};
}
/// Deleted: the binding keeps a reference to its source, so a temporary Property cannot be bound.
template <typename T, typename To, typename From>
auto bind(Property<T>&& source, To to, From from) = delete;

/// Several-source binding: `fn(a.get(), b.get(), ...)`, computed again whenever any of the
/// sources changes. The function comes first, so the sources after it can be any number of
/// Property and Computed values. A value used in more than one place is better declared once as a
/// Computed.
///
/// The result is one-way. A change that computes the same result costs what the target's setter
/// costs for an unchanged value, which is nothing. The binding remembers each source's last
/// value, so a source destroyed before the others stops contributing changes.
/// `fn` must be copyable, as for the one-source form.
///
/// Example:
/// @code
/// ui::Property<bool> dirty{false};
/// ui::Property<bool> busy{false};
///
/// ui::Button save{
///     .text    = "Save",
///     .enabled = ui::bind([](bool is_dirty, bool is_busy) { return is_dirty && !is_busy; }, dirty, busy),
/// };
/// @endcode
template <typename Fn, typename... Ss>
    requires(sizeof...(Ss) > 0) && (detail::is_source<Ss> && ...) &&
            std::invocable<Fn&, const detail::source_value_t<Ss>&...> && std::copy_constructible<Fn> &&
            (std::copy_constructible<detail::source_value_t<Ss>> && ...)
[[nodiscard]] auto bind(Fn fn, const Ss&... sources)
{
    using Result = std::remove_cvref_t<std::invoke_result_t<Fn&, const detail::source_value_t<Ss>&...>>;

    return Binding<Result>{fn(detail::as_property(sources).get()...),
                           [fn,
                            from = std::tuple<const Property<detail::source_value_t<Ss>>*...>{&detail::as_property(
                                sources)...}](Callback<void(Result)> sink, Result* now) mutable
                           {
                               if (now != nullptr)
                               {
                                   *now = std::apply([&](const auto*... source) { return fn(source->get()...); }, from);
                               }
                               if (!sink)
                               {
                                   return Subscription{};
                               }
                               return std::apply(
                                   [&](const auto*... source)
                                   {
                                       return ui::observe([fn, sink = std::move(sink)](
                                                              const detail::source_value_t<Ss>&... values) mutable
                                                          { sink(fn(values...)); },
                                                          *source...);
                                   },
                                   from);
                           }};
}

/// One-way binding of a Computed: its value, verbatim. Same as bind() of a `const Property<T>`.
template <typename T>
[[nodiscard]] Binding<T> bind(const Computed<T>& source)
{
    return bind(source.property());
}
/// One-way binding of a Computed with a transform, as bind(const Property<T>&, Fn).
template <typename T, typename Fn>
    requires std::invocable<Fn&, const T&> && std::copy_constructible<Fn>
[[nodiscard]] auto bind(const Computed<T>& source, Fn fn)
{
    return bind(source.property(), std::move(fn));
}

/// Deleted: a binding keeps a reference to its source, so a temporary Property cannot be bound.
template <typename T>
Binding<T> bind(const Property<T>&& source) = delete;
/// Deleted: a binding keeps a reference to its source, so a temporary Property cannot be bound.
template <typename T, typename Fn>
auto bind(const Property<T>&& source, Fn fn) = delete;
/// Deleted: a binding keeps a reference to its source, so a temporary Computed cannot be bound.
template <typename T>
Binding<T> bind(const Computed<T>&& source) = delete;
/// Deleted: a binding keeps a reference to its source, so a temporary Computed cannot be bound.
template <typename T, typename Fn>
auto bind(const Computed<T>&& source, Fn fn) = delete;

/// Deleted: the several-source form keeps a reference to each source, so none of them can be a
/// temporary.
template <typename Fn, typename... Sources>
    requires(sizeof...(Sources) > 0) && (detail::is_source<Sources> && ...) &&
                (std::is_rvalue_reference_v<Sources &&> || ...)
auto bind(Fn fn, Sources&&... sources) = delete;

} // namespace ui

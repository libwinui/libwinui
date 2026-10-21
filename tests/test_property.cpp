// SPDX-License-Identifier: MIT
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

namespace
{
// Owns its subscription the way Element::add_subscription() does, so dropping it unsubscribes.
struct Label
{
    std::string  text;
    Subscription sub;
};
} // namespace

TEST_CASE("subscriber destroyed first: a later write dispatches into nothing", "[property]")
{
    int           fired = 0;
    Property<int> p{0};

    {
        Label label;
        label.sub = p.observe(
            [&](const int& n)
            {
                ++fired;
                label.text = std::to_string(n);
            });
        p = 1;
        REQUIRE(label.text == "1");
        REQUIRE(p.observer_count() == 1);
    }

    REQUIRE(p.observer_count() == 0);
    p = 2;
    REQUIRE(fired == 1);
}

TEST_CASE("property destroyed first: the subscriber tears down safely", "[property]")
{
    int   fired = 0;
    Label outlives;

    {
        Property<int> p{0};
        outlives.sub = p.observe(
            [&](const int& n)
            {
                ++fired;
                outlives.text = std::to_string(n);
            });
        p = 5;
        REQUIRE(outlives.text == "5");
        REQUIRE(fired == 1);
    }

    // The Property nulled every subscriber's back-pointer as it died, so destroying
    // `outlives` at the end of this scope touches nothing dead.
    REQUIRE_FALSE(outlives.sub.alive());
}

TEST_CASE("the equality gate stops an unchanged write dead", "[property]")
{
    Property<int> p{7};
    int           fired = 0;
    auto          sub   = p.observe([&](const int&) { ++fired; });

    p = 7; // same value: no assignment, no dispatch, no cost
    REQUIRE(fired == 0);

    p = 8;
    REQUIRE(fired == 1);
}

TEST_CASE("a subscriber in the middle of the list can die without breaking it", "[property]")
{
    Property<int> p{0};
    int           a = 0, b = 0, c = 0;

    auto s1 = p.observe([&](const int&) { ++a; });
    {
        auto s2 = p.observe([&](const int&) { ++b; });
        auto s3 = p.observe([&](const int&) { ++c; });
        p       = 1;
        REQUIRE((a == 1 && b == 1 && c == 1));
    }
    p = 2;
    REQUIRE(a == 2);
    REQUIRE(b == 1);
    REQUIRE(c == 1);
}

TEST_CASE("a handler may unsubscribe itself from inside its own callback", "[property]")
{
    // The classic signal/slot crash: dispatch() must cache `next` before invoking, because
    // the handler it is invoking can unlink the node it is standing on.
    Property<int> p{0};
    int           self = 0, other = 0;

    Subscription a;
    Subscription b = p.observe([&](const int&) { ++other; });
    a              = p.observe(
        [&](const int&)
        {
            ++self;
            a.reset(); // unsubscribes mid-dispatch
        });

    p = 1;
    REQUIRE(self == 1);
    REQUIRE(other == 1);

    p = 2;
    REQUIRE(self == 1); // gone
    REQUIRE(other == 2);
}

TEST_CASE("subscriptions survive being moved, including vector reallocation", "[property]")
{
    // Element keeps subscriptions in a std::vector, which moves them as it grows. A move must
    // splice the intrusive links across, or a later write walks freed memory.
    Property<int>             p{0};
    std::vector<Subscription> subs;
    int                       fired = 0;

    for (int i = 0; i < 16; ++i)
    {
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): the reallocation is what this tests
        subs.push_back(p.observe([&](const int&) { ++fired; }));
    }
    REQUIRE(p.observer_count() == 16);

    p = 1;
    REQUIRE(fired == 16);

    subs.clear();
    REQUIRE(p.observer_count() == 0);
    p = 2;
    REQUIRE(fired == 16);
}

TEST_CASE("update() runs the equality gate on the result, not the mutation", "[property]")
{
    Property<std::vector<int>> p{{1, 2, 3}};
    int                        fired = 0;
    auto                       sub   = p.observe([&](const std::vector<int>&) { ++fired; });

    p.update([](std::vector<int>& v) { v.push_back(4); });
    REQUIRE(fired == 1);
    REQUIRE(p.get().size() == 4);

    p.update([](std::vector<int>&) { /* changes nothing */ });
    REQUIRE(fired == 1);
}

TEST_CASE("bind() carries the initial value and then tracks changes", "[property][binding]")
{
    Property<int> clicks{2};

    Binding<std::string> b = bind(clicks, [](int n) { return "Clicks: " + std::to_string(n); });
    REQUIRE(b.reactive());
    REQUIRE(b.initial() == "Clicks: 2");

    std::string  seen;
    Subscription sub = b.subscribe([&](std::string v) { seen = std::move(v); });
    clicks           = 3;
    REQUIRE(seen == "Clicks: 3");
}

TEST_CASE("current() reads what the source holds now, whatever the binding was made from", "[property][binding]")
{
    Property<int>                count{1};
    const Property<int>&         read_only = count;
    Computed<int>                twice{[](int n) { return n * 2; }, count};
    Property<bool>               flag{false};
    ObservableVector<int>        list{1, 2};
    const ObservableVector<int>& read_list = list;

    Binding<int>              plain     = bind(count);
    Binding<int>              const_one = bind(read_only);
    Binding<int>              computed  = bind(twice);
    Binding<std::string>      formatted = bind(count, [](int n) { return std::to_string(n); });
    Binding<int>              several   = bind([](int a, bool b) { return b ? a : -a; }, count, flag);
    Binding<double>           converted = bind(count);
    TwoWayBinding<bool>       choice    = selects(count, 5);
    Binding<std::vector<int>> items     = bind(list);
    Binding<std::vector<int>> copied    = bind(read_list);
    Binding<std::string>      constant  = "fixed";

    count = 5;
    flag  = true;
    list.push_back(3);
    const std::size_t observers = count.observer_count(); // the Computed's

    CHECK(plain.initial() == 1); // initial() stays what it was when the binding was made
    CHECK(plain.current() == 5);
    CHECK(const_one.current() == 5);
    CHECK(computed.current() == 10);
    CHECK(formatted.current() == "5");
    CHECK(several.current() == 5);
    CHECK(converted.current() == 5.0);
    CHECK(choice.current());
    CHECK(items.current() == std::vector<int>{1, 2, 3});
    CHECK(copied.current() == std::vector<int>{1, 2, 3});
    CHECK(constant.current() == "fixed");
    CHECK(count.observer_count() == observers); // reading subscribes nothing
}

TEST_CASE("a plain value makes a non-reactive Binding", "[property][binding]")
{
    Binding<std::string> literal = "Save";
    REQUIRE_FALSE(literal.reactive());
    REQUIRE(literal.initial() == "Save");

    Binding<bool> flag = true;
    REQUIRE_FALSE(flag.reactive());
    REQUIRE(flag.initial());
}

namespace
{
template <typename P>
concept bindable = requires(P&& source) { bind(std::forward<P>(source)); };

inline constexpr auto SumOfTwo = [](int a, int b) { return a + b; };
template <typename A, typename B>
concept bindable_pair = requires(A&& a, B&& b) { bind(SumOfTwo, std::forward<A>(a), std::forward<B>(b)); };
} // namespace

TEST_CASE("a handler may reset the subscription that is next in line", "[property]")
{
    Property<int> p{0};
    int           first = 0, second = 0, third = 0;

    Subscription c = p.observe([&](const int&) { ++third; });
    Subscription b = p.observe([&](const int&) { ++second; });
    Subscription a = p.observe(
        [&](const int&)
        {
            ++first;
            b.reset(); // newest runs first, so `b` is the one dispatch would call next
        });

    p = 1;
    REQUIRE(first == 1);
    REQUIRE(second == 0);
    REQUIRE(third == 1); // still reached, although the one before it went
}

TEST_CASE("a handler may move the subscription that is next in line", "[property]")
{
    Property<int>             p{0};
    int                       moved = 0, last = 0;
    std::vector<Subscription> held;
    held.reserve(1);

    Subscription c = p.observe([&](const int&) { ++last; });
    held.push_back(p.observe([&](const int&) { ++moved; }));
    Subscription a = p.observe(
        [&](const int&)
        {
            if (held.size() == 1)
            {
                held.emplace_back(); // reallocates: the next subscription changes its address
            }
        });

    p = 1;
    REQUIRE(moved == 1);
    REQUIRE(last == 1);
}

TEST_CASE("a handler may destroy the source it is called from", "[property]")
{
    auto source = std::make_unique<Property<int>>(0);
    int  after  = 0;

    Subscription b = source->observe([&](const int&) { ++after; });
    Subscription a = source->observe([&](const int&) { source.reset(); });

    *source = 1;
    REQUIRE(source == nullptr);
    REQUIRE(after == 0); // nobody is called with a value that no longer exists
    REQUIRE_FALSE(a.alive());
    REQUIRE_FALSE(b.alive());
}

TEST_CASE("a nested write from a handler keeps both dispatches intact", "[property]")
{
    Property<int>    p{0};
    std::vector<int> seen;

    Subscription b = p.observe([&](const int& v) { seen.push_back(v); });
    Subscription a = p.observe(
        [&](const int& v)
        {
            if (v == 1)
            {
                b.reset(); // while the outer dispatch waits on it
                p = 2;
            }
        });

    p = 1;
    REQUIRE(seen.empty());
    REQUIRE(p.observer_count() == 1);
}

TEST_CASE("a const Property can be observed and bound; a temporary cannot be bound", "[property][binding]")
{
    // Window::state() hands out exactly this: a property only its host writes.
    Property<int>        source{1};
    const Property<int>& view = source;

    int                seen  = 0;
    const Subscription watch = view.observe([&](const int& v) { seen = v; });
    source                   = 5;
    CHECK(seen == 5);

    Binding<int> binding = bind(view, [](int v) { return v * 2; });
    CHECK(binding.initial() == 10);
    CHECK(binding.reactive());
    CHECK(view.observer_count() == 1); // bind() subscribes only when a spec's build() asks

    STATIC_REQUIRE(bindable<Property<int>&>);
    STATIC_REQUIRE(bindable<const Property<int>&>);
    STATIC_REQUIRE_FALSE(bindable<Property<int>>); // would dangle: deleted
    // The several-source form keeps a reference to each source too.
    STATIC_REQUIRE(bindable_pair<Property<int>&, const Property<int>&>);
    STATIC_REQUIRE_FALSE(bindable_pair<Property<int>&, Property<int>>);
    STATIC_REQUIRE_FALSE(bindable_pair<Property<int>, Property<int>&>);
    // A std:: value type drags std::bind in through ADL; ours must still be the one chosen.
    STATIC_REQUIRE(std::same_as<decltype(bind(std::declval<Property<std::string>&>())), TwoWayBinding<std::string>>);
}

TEST_CASE("a binding converts to any field type its value converts to", "[property][binding]")
{
    SECTION("a function returning a string literal binds a string field")
    {
        Property<bool>       on{false};
        Binding<std::string> label = bind(on, [](bool v) { return v ? "On" : "Off"; });
        CHECK(label.initial() == "Off");
        CHECK_FALSE(label.writable());

        std::string        seen;
        const Subscription sub = label.subscribe([&](std::string v) { seen = std::move(v); });
        on                     = true;
        CHECK(seen == "On");
    }
    SECTION("an int property binds a double field one-way")
    {
        Property<int>   count{3};
        Binding<double> value = bind(count);
        CHECK(value.initial() == 3.0);
        CHECK(value.reactive());
        CHECK_FALSE(value.writable()); // a double does not go back into an int without loss

        double             seen = 0.0;
        const Subscription sub  = value.subscribe([&](double v) { seen = v; });
        count                   = 7;
        CHECK(seen == 7.0);
    }
    SECTION("a two-way bind of a bool property does not go into an editable optional bool field")
    {
        // The field can be unset, which the property cannot hold: one-way is said explicitly.
        STATIC_REQUIRE_FALSE(std::convertible_to<TwoWayBinding<bool>, TwoWayField<std::optional<bool>>>);
        // The same for an index: a selection that can be empty takes an optional property.
        STATIC_REQUIRE_FALSE(std::convertible_to<TwoWayBinding<std::size_t>, TwoWayField<std::optional<std::size_t>>>);
        STATIC_REQUIRE(std::convertible_to<TwoWayBinding<std::optional<bool>>, TwoWayField<std::optional<bool>>>);
        STATIC_REQUIRE(std::convertible_to<Binding<bool>, TwoWayField<std::optional<bool>>>);
        STATIC_REQUIRE(std::convertible_to<TwoWayBinding<bool>, TwoWayField<bool>>);

        Property<bool>                   done{true};
        TwoWayField<std::optional<bool>> checked = bind_one_way(done);
        CHECK(checked.initial() == std::optional<bool>{true});
        CHECK_FALSE(checked.writable());

        Property<std::optional<bool>>    state{false};
        TwoWayField<std::optional<bool>> both = bind(state);
        CHECK(both.writable());
    }
    SECTION("a bool property binds an optional bool display field one-way")
    {
        Property<bool>               done{true};
        Binding<std::optional<bool>> shown = bind(done);
        CHECK(shown.initial() == std::optional<bool>{true});
        CHECK_FALSE(shown.writable());
    }
    SECTION("a float property binds a double field two-way")
    {
        Property<float> level{0.5f};
        Binding<double> value = bind(level);
        REQUIRE(value.writable());
        auto write = value.take_writer();
        write(0.25);
        CHECK(level.get() == 0.25f);
    }
    STATIC_REQUIRE(std::convertible_to<Binding<int>, Binding<double>>);
    STATIC_REQUIRE_FALSE(std::convertible_to<Binding<double>, Binding<int>>); // would lose the fraction
    STATIC_REQUIRE_FALSE(std::convertible_to<Binding<int>, Binding<bool>>);
}

namespace
{
template <typename T>
concept CanAddAssign = requires(Property<T>& p, T v) { p += v; };
template <typename T>
concept CanSubAssign = requires(Property<T>& p, T v) { p -= v; };
template <typename T>
concept CanIncrement = requires(Property<T>& p) { ++p; };
template <typename T>
concept CanToggle = requires(Property<T>& p) { p.toggle(); };
} // namespace

static_assert(CanAddAssign<int> && CanSubAssign<int> && CanIncrement<int> && !CanToggle<int>);
static_assert(CanAddAssign<double> && CanIncrement<double>);
static_assert(CanAddAssign<std::string> && !CanSubAssign<std::string> && !CanIncrement<std::string>);
static_assert(!CanAddAssign<bool> && !CanSubAssign<bool> && !CanIncrement<bool> && CanToggle<bool>);

TEST_CASE("+= and -= write through set(), so the equality gate applies", "[property][arithmetic]")
{
    Property<int> p{10};
    int           fired = 0;
    int           seen  = 0;
    auto          sub   = p.observe(
        [&](const int& v)
        {
            ++fired;
            seen = v;
        });

    p += 5;
    REQUIRE(p.get() == 15);
    REQUIRE(fired == 1);
    REQUIRE(seen == 15);

    p -= 3;
    REQUIRE(p.get() == 12);
    REQUIRE(fired == 2);

    p += 0; // unchanged: nobody hears of it
    REQUIRE(fired == 2);

    Property<std::string> log{"a"};
    log += "b";
    REQUIRE(log.get() == "ab");
}

TEST_CASE("prefix ++/-- return the property, postfix the value from before", "[property][arithmetic]")
{
    Property<int> p{0};
    int           fired = 0;
    auto          sub   = p.observe([&](const int&) { ++fired; });

    REQUIRE((++p).get() == 1);
    REQUIRE(p++ == 1);
    REQUIRE(p.get() == 2);
    REQUIRE((--p).get() == 1);
    REQUIRE(p-- == 1);
    REQUIRE(p.get() == 0);
    REQUIRE(fired == 4);
}

TEST_CASE("toggle() flips a Property<bool> through set()", "[property][arithmetic]")
{
    Property<bool> on{false};
    int            fired = 0;
    auto           sub   = on.observe([&](const bool&) { ++fired; });

    on.toggle();
    REQUIRE(on.get());
    on.toggle();
    REQUIRE_FALSE(on.get());
    REQUIRE(fired == 2);
}

TEST_CASE("a copied binding subscribes each copy on its own", "[property][binding][copy]")
{
    Property<std::string> name{"Ada"};
    Binding<std::string>  first  = bind(name);
    Binding<std::string>  second = first; // NOLINT(performance-unnecessary-copy-initialization)
    REQUIRE(first.reactive());
    REQUIRE(second.reactive());
    CHECK(second.initial() == "Ada");

    Label a;
    Label b;
    a.sub = first.subscribe([&](std::string v) { a.text = std::move(v); });
    b.sub = second.subscribe([&](std::string v) { b.text = std::move(v); });
    REQUIRE(name.observer_count() == 2);

    name = "Grace";
    CHECK(a.text == "Grace");
    CHECK(b.text == "Grace");

    a.sub.reset(); // one control goes; the other stays bound
    name = "Linus";
    CHECK(a.text == "Grace");
    CHECK(b.text == "Linus");
}

TEST_CASE("two-way copies write back from each control, and the gate ends the echo", "[property][binding][copy]")
{
    Property<int> level{1};
    Binding<int>  binding  = bind(level);
    Binding<int>  copy     = binding;
    int           notified = 0;
    auto          count    = level.observe([&](const int&) { ++notified; });

    // Two controls that echo every value they are shown back through their writer, as an editable
    // control reports a change it was told about.
    struct Control
    {
        int                 shown = 0;
        Binding<int>::Write write;
        Subscription        sub;
    };
    Control a;
    Control b;
    a.write = binding.take_writer();
    b.write = copy.take_writer();
    REQUIRE(a.write);
    REQUIRE(b.write);
    a.sub = binding.subscribe(
        [&](int v)
        {
            a.shown = v;
            a.write(v);
        });
    b.sub = copy.subscribe(
        [&](int v)
        {
            b.shown = v;
            b.write(v);
        });

    a.write(5); // the user edits the first control
    CHECK(level.get() == 5);
    CHECK(b.shown == 5);
    CHECK(notified == 1);

    b.write(7); // and then the second
    CHECK(level.get() == 7);
    CHECK(a.shown == 7);
    CHECK(notified == 2);
}

TEST_CASE("copies of a binding go dead with their property", "[property][binding][copy]")
{
    Label a;
    Label b;
    {
        Property<int> count{0};
        Binding<int>  first  = bind(count);
        Binding<int>  second = first;
        a.sub                = first.subscribe([&](int v) { a.text = std::to_string(v); });
        b.sub                = second.subscribe([&](int v) { b.text = std::to_string(v); });
        count                = 3;
    }
    CHECK_FALSE(a.sub.alive());
    CHECK_FALSE(b.sub.alive());
    CHECK(b.text == "3");
}

TEST_CASE("a moved-from binding is a constant, the binding it moved to is reactive", "[property][binding][copy]")
{
    Property<int> count{2};
    Binding<int>  from = bind(count);
    Binding<int>  to   = std::move(from);
    CHECK(to.reactive());
    CHECK(to.writable());
    CHECK_FALSE(from.reactive()); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(from.writable()); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

    Binding<double> widened = to; // a converting copy leaves `to` as it was
    CHECK(widened.reactive());
    CHECK(to.reactive());
}

TEST_CASE("a Property written into a field binds it, as bind() does", "[property][binding][implicit]")
{
    Property<std::string> name{"Ada"};
    const Property<int>   fixed{3};
    Property<float>       level{0.5f};
    Property<bool>        flag{true};

    Binding<std::string> two_way = name;
    REQUIRE(two_way.reactive());
    REQUIRE(two_way.writable());
    REQUIRE(two_way.initial() == "Ada");

    Binding<int> one_way = fixed;
    REQUIRE(one_way.reactive());
    REQUIRE_FALSE(one_way.writable());

    // Converts as a bind() of it would: float to double stays two-way.
    Binding<double> widened = level;
    REQUIRE(widened.writable());
    auto write = widened.take_writer();
    write(0.25);
    REQUIRE(level.get() == 0.25f);

    // A Computed binds one-way.
    Computed<bool> inverted{[](bool b) { return !b; }, flag};
    Binding<bool>  shown = inverted;
    REQUIRE(shown.reactive());
    REQUIRE_FALSE(shown.writable());
    bool         seen = true;
    Subscription sub  = shown.subscribe([&](bool v) { seen = v; });
    flag              = false;
    REQUIRE(seen);

    // Lossy conversions are refused here as for plain values, and so is a two-way bind of a plain
    // property into a field that can be unset (TwoWayField).
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Property<int>&, Binding<bool>>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Property<bool>&, TwoWayField<std::optional<bool>>>);
    STATIC_REQUIRE(std::is_convertible_v<const Property<bool>&, TwoWayField<std::optional<bool>>>);
    STATIC_REQUIRE(std::is_convertible_v<Property<std::optional<bool>>&, TwoWayField<std::optional<bool>>>);
}

TEST_CASE("an editable field refuses a two-way bind that would lose what it writes back", "[property][binding]")
{
    // The refusal is a static_assert in the chosen constructor, so a compile test can only check the
    // trait that selects it: the field takes the property's values but its own do not fit back.
    STATIC_REQUIRE(detail::LossyWriteBack<int, double>);              // Slider value
    STATIC_REQUIRE(detail::LossyWriteBack<int, std::size_t>);         // an index
    STATIC_REQUIRE(detail::LossyWriteBack<unsigned, std::size_t>);    // a narrower unsigned
    STATIC_REQUIRE(detail::LossyWriteBack<const char*, std::string>); // text cannot go back at all
    STATIC_REQUIRE(detail::LossyWriteBack<std::optional<int>, std::optional<double>>);
    // Lossless pairs and the field's own type stay two-way.
    STATIC_REQUIRE_FALSE(detail::LossyWriteBack<float, double>);
    STATIC_REQUIRE_FALSE(detail::LossyWriteBack<double, float>);
    STATIC_REQUIRE_FALSE(detail::LossyWriteBack<std::size_t, std::size_t>);
    STATIC_REQUIRE_FALSE(detail::LossyWriteBack<std::optional<float>, std::optional<double>>);
    // A pair that does not convert at all is a different error, not this one.
    STATIC_REQUIRE_FALSE(detail::LossyWriteBack<std::string, double>);
    // A field that can be unset keeps its own refusal for a plain property.
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Property<std::size_t>&, TwoWayField<std::optional<std::size_t>>>);

    SECTION("a lossless pair writes back")
    {
        Property<float>     level{0.5f};
        TwoWayField<double> value = level;
        REQUIRE(value.writable());
        value.take_writer()(0.25);
        CHECK(level.get() == 0.25f);
    }
    SECTION("bind_one_way() of a lossy pair shows the property and writes nothing")
    {
        Property<int>       volume{40};
        TwoWayField<double> value = bind_one_way(volume);
        CHECK(value.initial() == 40.0);
        CHECK(value.reactive());
        CHECK_FALSE(value.writable());
    }
    SECTION("a display field still takes a writable property of another type, one-way")
    {
        Property<int>   volume{40};
        Binding<double> shown = volume;
        CHECK(shown.reactive());
        CHECK_FALSE(shown.writable());
    }
    SECTION("an optional field does not write a double into an optional int")
    {
        Property<std::optional<int>>   count{3};
        Binding<std::optional<double>> shown = bind(count);
        CHECK(shown.initial() == std::optional<double>{3.0});
        CHECK_FALSE(shown.writable());
    }
}

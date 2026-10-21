// SPDX-License-Identifier: MIT
#include "winui/core/Property.hpp"

#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("a Computed holds fn of its sources and follows every change", "[property][computed]")
{
    Property<std::string> first{"Ada"};
    Property<std::string> last;
    Property<bool>        agreed{false};

    Computed can_submit{[](const std::string& f, const std::string& l, bool ok)
                        { return !f.empty() && !l.empty() && ok; },
                        first,
                        last,
                        agreed};
    static_assert(std::same_as<decltype(can_submit), Computed<bool>>);
    REQUIRE_FALSE(can_submit.get());

    last   = "Lovelace";
    agreed = true;
    REQUIRE(can_submit.get());
    REQUIRE(*can_submit);

    first = "";
    REQUIRE_FALSE(can_submit.get());
}

TEST_CASE("a Computed's observers hear only results that changed", "[property][computed]")
{
    Property<int> n{1};
    Computed      even{[](int v) { return v % 2 == 0; }, n};

    int          fired = 0;
    Subscription sub   = even.observe([&](const bool&) { ++fired; });

    n = 3; // still odd: the gate stops it
    REQUIRE(fired == 0);
    n = 4;
    REQUIRE(fired == 1);
    n = 6;
    REQUIRE(fired == 1);
}

TEST_CASE("a Computed destroyed first leaves nothing behind on its sources", "[property][computed]")
{
    Property<int> a{1};
    Property<int> b{2};
    {
        Computed<int> sum{[](int x, int y) { return x + y; }, a, b};
        REQUIRE(a.observer_count() == 1);
        REQUIRE(b.observer_count() == 1);
        a = 5;
        REQUIRE(sum.get() == 7);
    }
    REQUIRE(a.observer_count() == 0);
    REQUIRE(b.observer_count() == 0);
    a = 6; // dispatches into nothing
    b = 7;
}

TEST_CASE("a source destroyed first keeps contributing its last value", "[property][computed]")
{
    Property<int> a{1};
    auto          b = std::make_unique<Property<int>>(2);
    Computed<int> sum{[](int x, int y) { return x + y; }, a, *b};

    *b = 10;
    REQUIRE(sum.get() == 11);
    b.reset();

    a = 5;
    REQUIRE(sum.get() == 15);
}

TEST_CASE("a Computed is a source of another Computed, of bind() and of observe()", "[property][computed]")
{
    Property<int> n{2};
    Computed<int> doubled{[](int v) { return v * 2; }, n};
    Computed<int> plus_one{[](int v) { return v + 1; }, doubled};
    REQUIRE(plus_one.get() == 5);

    Binding<int> verbatim = bind(doubled);
    REQUIRE(verbatim.reactive());
    REQUIRE_FALSE(verbatim.writable());
    REQUIRE(verbatim.initial() == 4);

    int          seen = 0;
    Subscription sub  = verbatim.subscribe([&](int v) { seen = v; });

    Binding<std::string> text  = bind(plus_one, [](int v) { return std::to_string(v); });
    std::string          shown = text.initial();
    Subscription         tsub  = text.subscribe([&](std::string v) { shown = std::move(v); });

    Binding<int> mixed      = bind([](int a, int b) { return a * 10 + b; }, n, doubled);
    int          mixed_seen = mixed.initial();
    Subscription msub       = mixed.subscribe([&](int v) { mixed_seen = v; });
    REQUIRE(mixed_seen == 24);

    n = 3;
    REQUIRE(plus_one.get() == 7);
    REQUIRE(seen == 6);
    REQUIRE(shown == "7");
    REQUIRE(mixed_seen == 36);
}

TEST_CASE("observe(fn, sources...) runs on any change, not for the current values", "[property][observe]")
{
    Property<std::string> first{"Ada"};
    Property<int>         age{36};

    std::string  line;
    int          calls = 0;
    Subscription sub   = observe(
        [&](const std::string& f, int a)
        {
            ++calls;
            line = f + " " + std::to_string(a);
        },
        first,
        age);
    REQUIRE(calls == 0);

    age = 37;
    REQUIRE(line == "Ada 37");
    first = "Grace";
    REQUIRE(line == "Grace 37");
    REQUIRE(calls == 2);

    sub.reset(); // drops every edge, not just the first
    REQUIRE(first.observer_count() == 0);
    REQUIRE(age.observer_count() == 0);
    age = 38;
    REQUIRE(calls == 2);
}

TEST_CASE("observe() keeps working when its first source dies", "[property][observe]")
{
    auto          a = std::make_unique<Property<int>>(1);
    Property<int> b{2};
    int           sum = 0;
    Subscription  sub = observe([&](int x, int y) { sum = x + y; }, *a, b);

    *a = 5;
    REQUIRE(sum == 7);
    a.reset();
    REQUIRE_FALSE(sub.alive());
    b = 10;
    REQUIRE(sum == 15); // the edge to `b` lives on in the state the dead first edge still holds
}

// SPDX-License-Identifier: MIT
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"

#include <algorithm>
#include <concepts>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;

namespace
{
// Every change a collection reports, in order.
struct Log
{
    std::vector<CollectionChange> changes;
    Subscription                  subscription;

    template <typename T>
    explicit Log(const ObservableVector<T>& source) :
        subscription(source.observe([this](const CollectionChange& change) { changes.push_back(change); }))
    {
    }

    [[nodiscard]] const CollectionChange& last() const
    {
        return changes.back();
    }
};

std::vector<std::string> content(const ObservableVector<std::string>& source)
{
    return {source.begin(), source.end()};
}
} // namespace

TEST_CASE("an ObservableVector reports each edit with its position and the new size", "[observable_vector]")
{
    ObservableVector<std::string> names{"a", "b", "c"};
    Log                           log{names};
    REQUIRE(names.size() == 3);
    REQUIRE(log.changes.empty()); // observing reports nothing by itself

    names.push_back("d");
    REQUIRE(log.changes.size() == 1);
    CHECK(log.last().kind == CollectionChangeKind::Inserted);
    CHECK(log.last().index == 3);
    CHECK(log.last().count == 1);
    CHECK(log.last().size == 4);

    names.insert(1, "x");
    CHECK(log.last().kind == CollectionChangeKind::Inserted);
    CHECK(log.last().index == 1);
    CHECK(content(names) == std::vector<std::string>{"a", "x", "b", "c", "d"});

    names.insert(99, "end"); // clamped to the end
    CHECK(log.last().index == 5);

    names.erase(1, 2);
    CHECK(log.last().kind == CollectionChangeKind::Removed);
    CHECK(log.last().index == 1);
    CHECK(log.last().count == 2);
    CHECK(log.last().size == 4);
    CHECK(content(names) == std::vector<std::string>{"a", "c", "d", "end"});

    names.erase(3, 10); // cut short at the end
    CHECK(log.last().count == 1);

    names.replace(0, "A");
    CHECK(log.last().kind == CollectionChangeKind::Replaced);
    CHECK(log.last().index == 0);
    CHECK(content(names) == std::vector<std::string>{"A", "c", "d"});

    names.move(0, 2);
    CHECK(log.last().kind == CollectionChangeKind::Moved);
    CHECK(log.last().index == 0);
    CHECK(log.last().to == 2);
    CHECK(content(names) == std::vector<std::string>{"c", "d", "A"});
    names.move(2, 1);
    CHECK(content(names) == std::vector<std::string>{"c", "A", "d"});

    const std::size_t before = log.changes.size();
    names.move(1, 1);  // nothing moved
    names.erase(7);    // nothing there
    names.erase(0, 0); // nothing asked for
    CHECK(log.changes.size() == before);

    names.assign(std::vector<std::string>{"p", "q"});
    CHECK(log.last().kind == CollectionChangeKind::Reset);
    CHECK(log.last().size == 2);

    names.clear();
    CHECK(log.last().kind == CollectionChangeKind::Reset);
    CHECK(log.last().size == 0);
    CHECK(names.empty());
}

TEST_CASE("an ObservableVector's items stay at their address while they are in it", "[observable_vector]")
{
    struct Task
    {
        explicit Task(std::string text) : title{std::move(text)}
        {
        }
        Property<std::string> title; // makes the item immovable
        Property<bool>        done{false};
    };

    ObservableVector<Task> tasks;
    Task&                  first  = tasks.emplace_back("first");
    Task&                  second = tasks.emplace_back("second");
    for (int i = 0; i < 100; ++i)
    {
        tasks.emplace(0, "filler"); // far past any reallocation of the storage
    }
    CHECK(first.title.get() == "first");
    CHECK(&tasks[100] == &first);
    CHECK(tasks.index_of(first) == 100);
    CHECK(tasks.index_of(second) == 101);

    tasks.move(101, 0);
    CHECK(&tasks[0] == &second);
    CHECK(tasks.index_of(second) == 0);

    CHECK(tasks.remove(second));
    CHECK(tasks.size() == 101);
    Task stranger{"not in the collection"};
    CHECK_FALSE(tasks.remove(stranger));
    CHECK(tasks.index_of(stranger) == std::nullopt);

    first.done = true;
    CHECK(tasks.erase_if([](const Task& task) { return !task.done.get(); }) == 100);
    REQUIRE(tasks.size() == 1);
    CHECK(&tasks.front() == &first);
}

TEST_CASE("a removed item is still alive while the observers are told", "[observable_vector]")
{
    struct Tracked
    {
        explicit Tracked(int* alive) : alive_(alive)
        {
            ++*alive_;
        }
        Tracked(const Tracked&)            = delete;
        Tracked& operator=(const Tracked&) = delete;
        ~Tracked()
        {
            --*alive_;
        }
        int* alive_;
    };

    int                       alive = 0;
    ObservableVector<Tracked> items;
    items.emplace_back(&alive);
    items.emplace_back(&alive);

    int          seen = -1;
    Subscription sub  = items.observe([&](const CollectionChange&) { seen = alive; });
    items.erase(0);
    CHECK(seen == 2); // what was built for the item goes first, then the item
    CHECK(alive == 1);
    items.clear();
    CHECK(seen == 1);
    CHECK(alive == 0);
}

TEST_CASE("an ObservableVector and its observers may be destroyed in either order", "[observable_vector]")
{
    SECTION("the observer first")
    {
        ObservableVector<int> numbers{1, 2};
        int                   calls = 0;
        {
            Subscription sub = numbers.observe([&](const CollectionChange&) { ++calls; });
            CHECK(numbers.observer_count() == 1);
            numbers.push_back(3);
        }
        CHECK(numbers.observer_count() == 0);
        numbers.push_back(4);
        CHECK(calls == 1);
    }
    SECTION("the collection first: a last reset to empty, then a dead subscription")
    {
        std::vector<CollectionChange> changes;
        Subscription                  sub;
        {
            ObservableVector<int> numbers{1, 2};
            sub = numbers.observe([&](const CollectionChange& change) { changes.push_back(change); });
            CHECK(sub.alive());
        }
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].kind == CollectionChangeKind::Reset);
        CHECK(changes[0].size == 0);
        CHECK_FALSE(sub.alive());
    }
}

TEST_CASE("bind of an ObservableVector names the collection, and gives whole copies to a plain field",
          "[observable_vector][binding]")
{
    ObservableVector<int>     numbers{1, 2};
    Binding<std::vector<int>> binding = ui::bind(numbers);
    CHECK(binding.observable() == &numbers);
    CHECK(binding.initial() == std::vector<int>{1, 2});
    REQUIRE(binding.reactive());

    std::vector<int> latest;
    Subscription     sub = binding.subscribe([&](std::vector<int> next) { latest = std::move(next); });
    numbers.push_back(3);
    CHECK(latest == std::vector<int>{1, 2, 3});

    // A vector or a Property names no collection.
    Property<std::vector<int>> property{std::vector<int>{4}};
    CHECK(ui::bind(property).observable() == nullptr);
    CHECK(Binding<std::vector<int>>{std::vector<int>{5}}.observable() == nullptr);
}

namespace
{
// Counts its copies, to show which bindings copy the collection.
struct Counted
{
    int        value = 0;
    static int copies;

    explicit Counted(int v) : value(v)
    {
    }
    Counted(const Counted& other) : value(other.value)
    {
        ++copies;
    }
    Counted& operator=(const Counted& other)
    {
        value = other.value;
        ++copies;
        return *this;
    }
};
int Counted::copies = 0;
} // namespace

TEST_CASE("bind of an ObservableVector copies it only when the vector is asked for", "[observable_vector][binding]")
{
    ObservableVector<Counted> items;
    items.push_back(Counted{1});
    items.push_back(Counted{2});

    Counted::copies                       = 0;
    Binding<std::vector<Counted>> binding = ui::bind(items);
    Binding<std::vector<Counted>> copy    = binding;
    CHECK(binding.observable() == &items);
    CHECK(Counted::copies == 0); // a field reading the collection in place never pays for one

    CHECK(binding.initial().size() == 2);
    CHECK(Counted::copies == 2);
    CHECK(binding.initial().size() == 2); // made once
    CHECK(Counted::copies == 2);

    items.push_back(Counted{3}); // the copy made later sees the collection as it is then
    CHECK(copy.initial().size() == 3);
}

TEST_CASE("bind of a const ObservableVector gives whole copies and names no collection", "[observable_vector][binding]")
{
    ObservableVector<int>        numbers{1, 2};
    const ObservableVector<int>& view    = numbers;
    Binding<std::vector<int>>    binding = ui::bind(view);
    CHECK(binding.observable() == nullptr);
    CHECK(binding.initial() == std::vector<int>{1, 2});
    REQUIRE(binding.reactive());
    CHECK_FALSE(binding.writable());

    std::vector<int> latest;
    Subscription     sub = binding.subscribe([&](std::vector<int> next) { latest = std::move(next); });
    numbers.push_back(3);
    CHECK(latest == std::vector<int>{1, 2, 3});
}

TEST_CASE("item_count follows an ObservableVector", "[observable_vector]")
{
    ItemCount fixed = 7;
    CHECK(fixed.initial() == 7);
    CHECK_FALSE(fixed.reactive());

    ObservableVector<int> numbers{1, 2};
    ItemCount             live = ui::item_count(numbers);
    CHECK(live.initial() == 2);
    REQUIRE(live.reactive());

    std::size_t  size = 0;
    Subscription sub  = live.subscribe([&](const CollectionChange& change) { size = change.size; });
    numbers.push_back(3);
    CHECK(size == 3);
}

namespace
{
struct Entry
{
    int         id = 0;
    std::string text;
    friend bool operator==(const Entry&, const Entry&) = default;
};

// Applies the changes an item_count() reports to a copy of the collection, the way a view applies
// them to its rows: a row keeps the item it was built for until it is replaced.
struct Mirror
{
    const Property<std::vector<Entry>>& source;
    std::vector<Entry>                  rows;
    int                                 moves    = 0;
    int                                 resets   = 0;
    int                                 replaced = 0;
    Subscription                        subscription;

    Mirror(const Property<std::vector<Entry>>& from, ItemCount count) : source(from), rows(from.get())
    {
        REQUIRE(count.initial() == rows.size());
        REQUIRE(count.reactive());
        subscription = count.subscribe(
            [this](const CollectionChange& change)
            {
                const std::vector<Entry>& now = source.get();
                const auto at = [this](std::size_t i) { return rows.begin() + static_cast<std::ptrdiff_t>(i); };
                switch (change.kind)
                {
                    case CollectionChangeKind::Inserted:
                        // Final positions: what was inserted is what the collection holds there now.
                        rows.insert(at(change.index),
                                    now.begin() + static_cast<std::ptrdiff_t>(change.index),
                                    now.begin() + static_cast<std::ptrdiff_t>(change.index + change.count));
                        break;
                    case CollectionChangeKind::Removed:
                        rows.erase(at(change.index), at(change.index + change.count));
                        break;
                    case CollectionChangeKind::Replaced:
                        replaced += static_cast<int>(change.count);
                        std::copy_n(now.begin() + static_cast<std::ptrdiff_t>(change.index), change.count, at(change.index));
                        break;
                    case CollectionChangeKind::Moved:
                    {
                        ++moves;
                        Entry moved = rows[change.index];
                        rows.erase(at(change.index));
                        rows.insert(at(change.to), std::move(moved));
                        break;
                    }
                    case CollectionChangeKind::Reset:
                        ++resets;
                        rows = now;
                        break;
                }
                REQUIRE(change.size == rows.size());
            });
    }
};
} // namespace

TEST_CASE("item_count of a keyed property reports a reorder as moves", "[observable_vector][key]")
{
    Property<std::vector<Entry>> model{std::vector<Entry>{{1, "a"}, {2, "b"}, {3, "c"}, {4, "d"}}};
    Mirror                       mirror{model, ui::item_count(model, &Entry::id)};

    // The first item to the end is one move, not three.
    model.update([](std::vector<Entry>& v) { std::rotate(v.begin(), v.begin() + 1, v.end()); });
    CHECK(mirror.rows == model.get());
    CHECK(mirror.moves == 1);
    CHECK(mirror.replaced == 0);

    // An edited item is replaced where it is; nothing moves.
    model.update([](std::vector<Entry>& v) { v[0].text = "B"; });
    CHECK(mirror.rows == model.get());
    CHECK(mirror.moves == 1);
    CHECK(mirror.replaced == 1);

    model.update([](std::vector<Entry>& v) { v.clear(); });
    CHECK(mirror.rows.empty());
    CHECK(mirror.resets == 0);
}

TEST_CASE("item_count of a keyed property follows any sequence of edits", "[observable_vector][key]")
{
    std::vector<Entry> start;
    start.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        start.push_back({i, "entry " + std::to_string(i)});
    }
    Property<std::vector<Entry>> model{start};
    Mirror                       mirror{model, ui::item_count(model, [](const Entry& e) { return e.id; })};

    std::mt19937 random{2028}; // NOLINT(bugprone-random-generator-seed): fixed, so a failure reproduces
    int          next_id = 100;
    for (int round = 0; round < 300; ++round)
    {
        std::vector<Entry> next = model.get();
        if (random() % 3 != 0)
        {
            std::shuffle(next.begin(), next.end(), random);
        }
        for (std::size_t n = random() % 4; n > 0 && !next.empty(); --n)
        {
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(random() % next.size()));
        }
        for (std::size_t n = random() % 4; n > 0; --n)
        {
            const int id = next_id++;
            next.insert(next.begin() + static_cast<std::ptrdiff_t>(random() % (next.size() + 1)), {id, "new"});
        }
        if (!next.empty() && random() % 2 == 0)
        {
            next[random() % next.size()].text += "!";
        }
        model = next;
        REQUIRE(mirror.rows == model.get());
    }
    CHECK(mirror.resets == 0);
}

TEST_CASE("item_count of a property without a key reports the edited run", "[observable_vector]")
{
    Property<std::vector<int>>    model{std::vector<int>{1, 2, 3}};
    std::vector<CollectionChange> changes;
    ItemCount                     count = ui::item_count(model);
    Subscription sub = count.subscribe([&](const CollectionChange& change) { changes.push_back(change); });

    model.update([](std::vector<int>& v) { v.push_back(4); });
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == CollectionChangeKind::Inserted);
    CHECK(changes[0].index == 3);
    CHECK(changes[0].size == 4);

    model.update([](std::vector<int>& v) { v.erase(v.begin() + 1); });
    REQUIRE(changes.size() == 2);
    CHECK(changes[1].kind == CollectionChangeKind::Removed);
    CHECK(changes[1].index == 1);
    CHECK(changes[1].size == 3);

    model.update([](std::vector<int>& v) { v[0] = 9; });
    REQUIRE(changes.size() == 3);
    CHECK(changes[2].kind == CollectionChangeKind::Replaced);
    CHECK(changes[2].index == 0);
}

TEST_CASE("erase_if reports each run of neighbors once, and survives an observer that edits", "[observable]")
{
    ObservableVector<int>         numbers{1, 2, 2, 3, 2, 4};
    std::vector<CollectionChange> changes;
    Subscription                  watch = numbers.observe([&](const CollectionChange& c) { changes.push_back(c); });

    CHECK(numbers.erase_if([](const int& n) { return n == 2; }) == 3);
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].index == 4);
    CHECK(changes[0].count == 1);
    CHECK(changes[1].index == 1);
    CHECK(changes[1].count == 2);
    CHECK(std::vector<int>(numbers.begin(), numbers.end()) == std::vector<int>{1, 3, 4});

    // An observer that empties the collection on the first removal leaves nothing to index into.
    ObservableVector<int> doomed{5, 1, 5, 1, 5};
    Subscription          wipe = doomed.observe(
        [&](const CollectionChange& c)
        {
            if (c.kind == CollectionChangeKind::Removed)
            {
                doomed.clear();
            }
        });
    CHECK(doomed.erase_if([](const int& n) { return n == 5; }) == 1);
    CHECK(doomed.empty());
}

TEST_CASE("the edges of ObservableVector and ItemCount", "[observable]")
{
    ObservableVector<int> numbers;
    int                   resets = 0;
    Subscription          watch  = numbers.observe([&](const CollectionChange&) { ++resets; });

    numbers.assign(std::vector<int>{}); // empty onto empty: nothing happened
    CHECK(resets == 0);
    numbers.assign({1, 2, 3});
    CHECK(resets == 1);

    const ObservableVector<int>& view = numbers;
    CHECK(view.front() == 1);
    CHECK(view.back() == 3);

    // An observer that removes the item again must not leave replace() indexing past the end.
    Subscription drop = numbers.observe(
        [&](const CollectionChange& c)
        {
            if (c.kind == CollectionChangeKind::Replaced)
            {
                numbers.clear();
            }
        });
    numbers.replace(2, 9);
    CHECK(numbers.empty());

    CHECK(ItemCount{-1}.initial() == 0); // a "not found" index is no items, not SIZE_MAX of them
    CHECK(ItemCount{3}.initial() == 3);
    STATIC_REQUIRE_FALSE(std::constructible_from<ItemCount, bool>);
}

TEST_CASE("count() follows the size of the collection, after its observers", "[observable_vector][binding]")
{
    ObservableVector<int> v{1, 2};
    CHECK(v.count().get() == 2);

    std::vector<std::size_t> seen;
    Subscription             s              = v.count().observe([&](std::size_t n) { seen.push_back(n); });
    std::size_t              size_when_told = 99;
    Subscription             c = v.observe([&](const CollectionChange&) { size_when_told = v.count().get(); });

    v.push_back(3);
    CHECK(size_when_told == 2); // the collection's own observers come first
    v.erase(0, 2);
    v.replace(0, 7); // the size stays, so count() does not change
    v.clear();
    CHECK(seen == std::vector<std::size_t>{3, 1, 0});

    const Binding<bool> empty = ui::bind(v.count(), [](std::size_t n) { return n == 0; });
    CHECK(empty.initial());
}

TEST_CASE("batch() reports its edits as one Reset when it returns", "[observable_vector]")
{
    ObservableVector<std::string> v{"a", "b", "c"};
    std::vector<CollectionChange> changes;
    Subscription                  s = v.observe([&](const CollectionChange& c) { changes.push_back(c); });
    std::vector<std::size_t>      counts;
    Subscription                  n = v.count().observe([&](std::size_t size) { counts.push_back(size); });

    v.batch(
        [&]
        {
            v.erase(0);
            v.insert(0, "z");
            v.batch([&] { v.push_back("d"); }); // nested: still one report, at the end
            CHECK(changes.empty());
        });
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == CollectionChangeKind::Reset);
    CHECK(changes[0].size == 4);
    CHECK(counts == std::vector<std::size_t>{4});
    CHECK(v[0] == "z");
    CHECK(v[3] == "d");

    v.batch([] {}); // nothing changed, nothing reported
    CHECK(changes.size() == 1);
}

TEST_CASE("An item removed in a batch lives until the batch has reported", "[observable_vector]")
{
    ObservableVector<std::string> v{"first", "second"};
    const std::string*            removed         = &v[0];
    bool                          alive_when_told = false;
    Subscription s = v.observe([&](const CollectionChange&) { alive_when_told = (*removed == "first"); });

    v.batch([&] { v.erase(0); });
    CHECK(alive_when_told);
    CHECK(v.size() == 1);
}

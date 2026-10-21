// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ForEach.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Dynamic.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
// A subtree in a 200 x 100 scroll view, laid out.
struct Hosted
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    ScrollControl        view;

    explicit Hosted(ui::Spec content)
    {
        view.set_child(std::move(content).build());
        layout();
    }
    void layout()
    {
        (void)view.measure({200.0f, 100.0f}, measurer, theme);
        view.arrange(Rect{0.0f, 0.0f, 200.0f, 100.0f});
    }
};

Size measured(Element& root)
{
    // Static: a list re-measures between layouts (scroll_into_view()) with the ones it was measured with.
    static HeadlessTextMeasurer measurer;
    return root.measure({400.0f, 300.0f}, measurer, Theme::default_light());
}

std::string text_of(const Element* element)
{
    const auto* text = dynamic_cast<const TextControl*>(element);
    return text != nullptr ? text->text() : std::string{"<not a text>"};
}
} // namespace

// ---- empty children ----

TEST_CASE("an empty Spec in a child list is no child at all", "[spec][dynamic]")
{
    const bool admin = false;

    ui::Control<ui::VerticalStack> stack;
    std::unique_ptr<Element>       tree = ui::build(ui::VerticalStack{
        .control = stack,
        .content =
            {
                ui::Text{.text = "Report"},
                admin ? ui::Spec{ui::Button{.text = "Delete all"}} : ui::Spec{},
                ui::when(admin, ui::Button{.text = "Audit"}),
                ui::when(!admin, ui::Text{.text = "Read only"}),
                ui::Spec{},
            },
    });
    REQUIRE(stack->children().size() == 2);
    CHECK(text_of(stack->children()[1].get()) == "Read only");

    ui::SpecList list = ui::content(ui::Spec{}, ui::Text{.text = "one"}, ui::when(false, ui::Text{.text = "two"}));
    CHECK(list.size() == 1);
}

TEST_CASE("ui::when does not build what it leaves out", "[spec][dynamic]")
{
    struct Counting
    {
        int*                                   built;
        [[nodiscard]] std::unique_ptr<Element> build() &&
        {
            ++*built;
            return std::make_unique<TextControl>("x");
        }
    };

    int built = 0;
    CHECK_FALSE(ui::when(false, Counting{&built}));
    CHECK(built == 0);
    CHECK(ui::when(true, Counting{&built}));
    CHECK(built == 1);
}

TEST_CASE("ui::when leaves no live handle into a subtree it throws away", "[spec][dynamic]")
{
    // The eager form: the Stack's children are built while the argument is evaluated, then
    // discarded with it; the handle they published must not dangle.
    ui::Control<ui::Button> eager;
    CHECK_FALSE(ui::when(false, ui::VerticalStack{.content = {ui::Button{.text = "a", .control = eager}}}));
    CHECK_FALSE(eager);
    CHECK(eager.get() == nullptr);

    // The lazy form builds nothing at all.
    ui::Control<ui::Button> lazy;
    int                     made = 0;
    const auto              make = [&]
    {
        ++made;
        return ui::VerticalStack{.content = {ui::Button{.text = "b", .control = lazy}}};
    };
    CHECK_FALSE(ui::when(false, make));
    CHECK(made == 0);
    CHECK_FALSE(lazy);

    ui::Spec shown = ui::when(true, make);
    CHECK(made == 1);
    REQUIRE(lazy);
    std::unique_ptr<Element> tree = std::move(shown).build();
    CHECK(lazy->text() == "b");

    // A copy follows the same control, and both empty once it is destroyed.
    ui::Control<ui::Button> copy = lazy;
    CHECK(copy.get() == lazy.get());
    tree.reset();
    CHECK_FALSE(lazy);
    CHECK_FALSE(copy);
}

// ---- visible ----

TEST_CASE("visible collapses a control without destroying it", "[spec][dynamic][visible]")
{
    Property<bool>           advanced{false};
    ui::Control<ui::TextBox> box;
    ui::Control<ui::Text>    fixed;
    ui::Control<ui::Text>    always;
    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .content = {
            ui::Text{.text = "always", .control = always},
            ui::Text{.text = "never", .visible = false, .control = fixed},
            ui::TextBox{.visible = ui::bind(advanced), .control = box},
        }});

    CHECK(always->visibility() == Visibility::Visible);
    CHECK(fixed->visibility() == Visibility::Collapsed);
    CHECK(box->visibility() == Visibility::Collapsed);
    const float hidden = measured(*tree).height;

    advanced = true;
    CHECK(box->visibility() == Visibility::Visible);
    CHECK(measured(*tree).height > hidden);

    // What the user typed survives being hidden: it is the same control.
    box->set_text("typed");
    const TextBoxControl* before = box;
    advanced                     = false;
    CHECK(measured(*tree).height == hidden);
    advanced = true;
    CHECK(static_cast<const TextBoxControl*>(box) == before);
    CHECK(box->text() == "typed");
}

TEST_CASE("a focused control that is collapsed gives up the focus", "[spec][dynamic][visible][focus]")
{
    Property<bool>          shown{true};
    ui::Control<ui::Button> first;
    ui::Control<ui::Button> second;

    HeadlessUI ui = create_headless();
    ui.window().set_content(ui::VerticalStack{
        .content = {
            ui::Button{.text = "First", .visible = ui::bind(shown), .control = first},
            ui::Button{.text = "Second", .control = second},
        }});
    ui.layout({400, 300});

    Window& window = ui.window();
    window.focus_content();
    REQUIRE(window.focused() == first);

    shown = false;
    ui.layout({400, 300});
    CHECK(window.focused() != first);
    CHECK_FALSE(first->has_focus_within());

    // It is out of the tab ring for as long as it is collapsed, and back in afterwards.
    window.focus_content();
    CHECK(window.focused() == second);
    shown = true;
    ui.layout({400, 300});
    window.focus_content();
    CHECK(window.focused() == first);
}

// ---- several-source bind ----

TEST_CASE("bind over several properties recomputes when any of them changes", "[property][binding]")
{
    Property<bool>        dirty{false};
    Property<bool>        busy{false};
    Property<std::string> name{"report"};

    int  computed = 0;
    auto binding  = ui::bind(
        [&computed](bool is_dirty, bool is_busy, const std::string& title)
        {
            ++computed;
            return is_dirty && !is_busy ? "Save " + title : std::string{};
        },
        dirty,
        busy,
        name);
    static_assert(std::is_same_v<decltype(binding), Binding<std::string>>);
    CHECK(binding.initial().empty());
    REQUIRE(binding.reactive());
    CHECK_FALSE(binding.writable());
    CHECK(computed == 1);

    std::vector<std::string> seen;
    {
        Subscription sub = binding.subscribe([&](std::string value) { seen.push_back(std::move(value)); });
        CHECK(dirty.observer_count() == 1);
        CHECK(busy.observer_count() == 1);
        CHECK(name.observer_count() == 1);

        dirty = true;
        CHECK(seen == std::vector<std::string>{"Save report"});
        name = "letter";
        busy = true;
        CHECK(seen == std::vector<std::string>{"Save report", "Save letter", ""});

        // Once per change of a source, never for a write that changes nothing.
        const int before = computed;
        busy             = true;
        CHECK(computed == before);
        busy = false;
        CHECK(computed == before + 1);
    }
    // Dropping the one subscription drops every edge.
    CHECK(dirty.observer_count() == 0);
    CHECK(busy.observer_count() == 0);
    CHECK(name.observer_count() == 0);
}

TEST_CASE("a several-source binding outlives any of its sources", "[property][binding]")
{
    Property<int> a{1};
    int           latest = 0;
    Subscription  sub;
    {
        Property<int> first{10};
        Property<int> last{100};
        auto          binding = ui::bind([](int x, int y, int z) { return x + y + z; }, first, a, last);
        CHECK(binding.initial() == 111);
        sub  = binding.subscribe([&](int value) { latest = value; });
        last = 200;
        CHECK(latest == 211);
    }
    // The first source (whose edge owns the rest) and the last one are gone; the survivor still
    // computes with their last values.
    a = 2;
    CHECK(latest == 212);
    sub.reset();
    CHECK(a.observer_count() == 0);
}

TEST_CASE("a spec field takes a several-source binding", "[spec][binding]")
{
    Property<bool>           dirty{false};
    Property<bool>           busy{false};
    ui::Control<ui::Button>  save;
    std::unique_ptr<Element> tree = ui::build(ui::Button{
        .text    = "Save",
        .visible = ui::bind([](bool is_dirty, bool is_busy) { return is_dirty && !is_busy; }, dirty, busy),
        .control = save,
    });
    CHECK(save->visibility() == Visibility::Collapsed);
    dirty = true;
    CHECK(save->visibility() == Visibility::Visible);
    busy = true;
    CHECK(save->visibility() == Visibility::Collapsed);
    tree.reset();
    CHECK(dirty.observer_count() == 0);
    CHECK(busy.observer_count() == 0);
}

// ---- Lazy ----

TEST_CASE("Lazy builds its content the first time it shows, and once", "[spec][dynamic][lazy]")
{
    Property<bool>           open{false};
    int                      built = 0;
    ui::Control<ui::Lazy>    lazy;
    ui::Control<ui::TextBox> box;
    std::unique_ptr<Element> tree = ui::build(ui::Lazy{
        .visible = ui::bind(open),
        .control = lazy,
        .content = [&]() -> ui::Spec
        {
            ++built;
            return ui::TextBox{.control = box};
        },
    });

    CHECK(built == 0);
    CHECK(lazy->child() == nullptr);
    CHECK(lazy->visibility() == Visibility::Collapsed);
    CHECK(measured(*tree).height == 0.0f);

    open = true;
    REQUIRE(built == 1);
    REQUIRE(lazy->child() != nullptr);
    CHECK(lazy->visibility() == Visibility::Visible);
    box->set_text("kept");

    const Element* child = lazy->child();
    for (int i = 0; i < 10; ++i)
    {
        open.toggle();
    }
    CHECK(built == 1);
    CHECK(lazy->child() == child);
    CHECK(box->text() == "kept");
}

TEST_CASE("Lazy with unload destroys its content while hidden", "[spec][dynamic][lazy]")
{
    Property<bool>           open{true};
    int                      built = 0;
    ui::Control<ui::Lazy>    lazy;
    std::unique_ptr<Element> tree = ui::build(ui::Lazy{
        .visible = ui::bind(open),
        .unload  = true,
        .control = lazy,
        .content = [&]() -> ui::Spec
        {
            ++built;
            return ui::Text{.text = "log"};
        },
    });
    CHECK(built == 1); // visible from the start: built at once
    open = false;
    CHECK(lazy->child() == nullptr);
    open = true;
    CHECK(built == 2);
    CHECK(lazy->child() != nullptr);

    // A Lazy that is simply visible is its content, built once.
    int                      plain = 0;
    std::unique_ptr<Element> fixed = ui::build(ui::Lazy{.content = [&]() -> ui::Spec
                                                        {
                                                            ++plain;
                                                            return ui::Text{.text = "x"};
                                                        }});
    CHECK(plain == 1);
}

// ---- Match with keep ----

TEST_CASE("Match with keep returns to the subtree it built before", "[spec][dynamic][match]")
{
    Property<int>                           page{0};
    int                                     built = 0;
    std::map<int, ui::Control<ui::TextBox>> boxes;
    ui::Control<ui::Match<int>>             host;

    std::unique_ptr<Element> tree = ui::build(ui::Match{
        .value   = ui::bind(page),
        .keep    = true,
        .control = host,
        .content = [&](const int& p) -> ui::Spec
        {
            ++built;
            return ui::TextBox{.control = boxes[p]};
        },
    });
    REQUIRE(built == 1);
    const TextBoxControl* first = boxes[0];
    boxes[0]->set_text("typed on page 0");
    CHECK(first->is_effectively_visible());

    page = 1;
    REQUIRE(built == 2);
    CHECK_FALSE(first->is_effectively_visible());
    CHECK(boxes[1]->is_effectively_visible());

    page = 0;
    CHECK(built == 2); // nothing is built for a value seen before
    CHECK(static_cast<const TextBoxControl*>(boxes[0]) == first);
    CHECK(first->text() == "typed on page 0");
    CHECK(first->is_effectively_visible());
    CHECK_FALSE(boxes[1]->is_effectively_visible());
}

TEST_CASE("Match with keep takes the room of the current page only", "[spec][dynamic][match]")
{
    const auto pages = [](const int& p) -> ui::Spec
    { return ui::Text{.text = "page", .width = 50.0f, .height = p == 0 ? 20.0f : 80.0f}; };

    Property<int> kept_page{0};
    Property<int> plain_page{0};
    auto          kept = ui::build(
        ui::VerticalStack{.content = ui::Match{.value = ui::bind(kept_page), .keep = true, .content = pages}});
    auto plain = ui::build(ui::VerticalStack{.content = ui::Match{.value = ui::bind(plain_page), .content = pages}});
    CHECK(measured(*kept) == measured(*plain));

    kept_page  = 1;
    plain_page = 1;
    CHECK(measured(*kept) == measured(*plain));
    kept_page  = 0;
    plain_page = 0;
    CHECK(measured(*kept) == measured(*plain));
}

TEST_CASE("Match without keep builds the subtree again", "[spec][dynamic][match]")
{
    Property<int>            page{0};
    int                      built = 0;
    std::unique_ptr<Element> tree  = ui::build(
        ui::Match{.value   = ui::bind(page),
                  .content = [&](const int&) -> ui::Spec
                  {
                      ++built;
                      return ui::Text{.text = "p"};
                  }});
    page = 1;
    page = 0;
    CHECK(built == 3);
}

// ---- State ----

TEST_CASE("State gives a component state that lives as long as its subtree", "[spec][dynamic][state]")
{
    struct Counter
    {
        Property<int> clicks{0};
    };

    int                     ran = 0;
    ui::Control<ui::Button> button;
    ui::Control<ui::Text>   label;
    const auto              counter = [&]() -> ui::Spec
    {
        return ui::State<Counter>{
            .content = [&](Counter& state) -> ui::Spec
            {
                ++ran;
                return ui::VerticalStack{
                    .content = {
                        ui::Text{.text = ui::bind(state.clicks, [](int n) { return std::to_string(n); }), .control = label},
                        ui::Button{.text = "Add", .on_click = [&state] { ++state.clicks; }, .control = button},
                    }};
            },
        };
    };

    std::unique_ptr<Element> tree = counter().build();
    REQUIRE(ran == 1);
    CHECK(label->text() == "0");
    ui::automation_providers(*button).invoke->invoke();
    ui::automation_providers(*button).invoke->invoke();
    CHECK(label->text() == "2");
    CHECK(ran == 1); // the content is built once; what changes is what was bound
}

TEST_CASE("State starts from .initial and is destroyed after its subtree", "[spec][dynamic][state]")
{
    static std::string order;
    order.clear();

    struct Probe
    {
        Property<int> value;
        explicit Probe(int start) : value{start}
        {
        }
        ~Probe()
        {
            order += "state;";
        }
    };
    class Leaf final : public Element
    {
    public:
        ~Leaf() override
        {
            order += "subtree;";
        }

    protected:
        [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
        {
            return {};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }
    };

    int                      seen = 0;
    std::unique_ptr<Element> tree = ui::build(ui::State<Probe>{
        .initial = [] { return Probe{42}; },
        .content = [&](Probe& probe) -> ui::Spec
        {
            seen = probe.value.get();
            return std::make_unique<Leaf>();
        },
    });
    CHECK(seen == 42);
    CHECK(order.empty());
    tree.reset();
    CHECK(order == "subtree;state;");
}

// ---- ForEach over an ObservableVector ----

namespace
{
struct Todo
{
    explicit Todo(std::string text) : title{std::move(text)}
    {
    }
    Property<std::string> title;
    Property<bool>        done{false};
};

std::vector<std::string> titles(const ForEachControl& rows)
{
    std::vector<std::string> out;
    out.reserve(rows.item_count());
    for (std::size_t i = 0; i < rows.item_count(); ++i)
    {
        out.push_back(text_of(rows.try_get_element(i)));
    }
    return out;
}
} // namespace

TEST_CASE("ForEach over an ObservableVector builds and destroys one row per edit", "[spec][dynamic][foreach]")
{
    ObservableVector<Todo> todos;
    todos.emplace_back("a");
    todos.emplace_back("b");
    todos.emplace_back("c");

    int  built = 0;
    auto spec  = ui::ForEach{
        .items = ui::bind(todos),
        .item  = [&built](Todo& todo) -> ui::Spec
        {
            ++built;
            return ui::Text{.text = ui::bind(todo.title), .height = 20.0f};
        },
    };
    static_assert(std::is_same_v<decltype(spec), ui::ForEach<Todo>>);

    ui::Control<ui::ForEach<Todo>> rows;
    spec.control = rows;
    Hosted host{std::move(spec)};
    REQUIRE(built == 3);
    CHECK(titles(*rows) == std::vector<std::string>{"a", "b", "c"});
    const Element* a = rows->try_get_element(0);
    const Element* b = rows->try_get_element(1);
    const Element* c = rows->try_get_element(2);

    // Insert: one row is built, the others are the elements they were.
    todos.emplace(1, "new");
    host.layout();
    CHECK(built == 4);
    CHECK(titles(*rows) == std::vector<std::string>{"a", "new", "b", "c"});
    CHECK(rows->try_get_element(0) == a);
    CHECK(rows->try_get_element(2) == b);
    CHECK(rows->try_get_element(3) == c);

    // An item's own property reaches its row without touching the collection.
    todos[2].title = "B";
    CHECK(built == 4);
    CHECK(text_of(b) == "B");

    // Move: the row goes with its item, state and all.
    todos.move(3, 0);
    host.layout();
    CHECK(built == 4);
    CHECK(titles(*rows) == std::vector<std::string>{"c", "a", "new", "B"});
    CHECK(rows->try_get_element(0) == c);
    CHECK(rows->try_get_element(1) == a);
    CHECK(rows->try_get_element(3) == b);
    todos.move(0, 2);
    host.layout();
    CHECK(built == 4);
    CHECK(titles(*rows) == std::vector<std::string>{"a", "new", "c", "B"});
    CHECK(rows->try_get_element(2) == c);

    // Remove: nothing is built.
    todos.erase(1);
    host.layout();
    CHECK(built == 4);
    CHECK(titles(*rows) == std::vector<std::string>{"a", "c", "B"});
    CHECK(rows->try_get_element(0) == a);
    CHECK(rows->try_get_element(1) == c);

    // Clear, then start again.
    todos.clear();
    host.layout();
    CHECK(rows->item_count() == 0);
    CHECK(rows->realized_count() == 0);
    todos.emplace_back("again");
    host.layout();
    CHECK(titles(*rows) == std::vector<std::string>{"again"});
}

TEST_CASE("ForEach rebuilds one row for a replaced item", "[spec][dynamic][foreach]")
{
    ObservableVector<std::string>         names{"a", "b", "c"};
    int                                   built = 0;
    ui::Control<ui::ForEach<std::string>> rows;
    Hosted                                host{ui::ForEach{
        .items   = ui::bind(names),
        .control = rows,
        .item    = [&built](const std::string& name) -> ui::Spec
        {
            ++built;
            return ui::Text{.text = name, .height = 20.0f};
        },
    }};
    const Element*                        a = rows->try_get_element(0);
    const Element*                        c = rows->try_get_element(2);

    names.replace(1, "B");
    host.layout();
    CHECK(built == 4);
    CHECK(titles(*rows) == std::vector<std::string>{"a", "B", "c"});
    CHECK(rows->try_get_element(0) == a);
    CHECK(rows->try_get_element(2) == c);
}

TEST_CASE("a ForEach row can remove its own item", "[spec][dynamic][foreach]")
{
    ObservableVector<Todo> todos;
    todos.emplace_back("a");
    todos.emplace_back("b");

    std::vector<ButtonControl*>    buttons;
    ui::Control<ui::ForEach<Todo>> rows;
    Hosted                         host{ui::ForEach{
        .items   = ui::bind(todos),
        .control = rows,
        .item    = [&](Todo& todo) -> ui::Spec
        {
            ui::Control<ui::Button> button;
            ui::Spec                row = ui::Button{.text     = ui::bind(todo.title),
                                                     .on_click = [&todos, &todo] { todos.remove(todo); },
                                                     .control  = button};
            buttons.push_back(button);
            return row;
        },
    }};
    REQUIRE(buttons.size() == 2);

    ui::automation_providers(*buttons[0]).invoke->invoke();
    host.layout();
    REQUIRE(todos.size() == 1);
    CHECK(todos[0].title.get() == "b");
    CHECK(rows->item_count() == 1);
    CHECK(rows->try_get_element(0) == buttons[1]);
}

TEST_CASE("ForEach and its ObservableVector may be destroyed in either order", "[spec][dynamic][foreach]")
{
    SECTION("the tree first")
    {
        ObservableVector<Todo> todos;
        todos.emplace_back("a");
        {
            Hosted host{ui::ForEach{.items = ui::bind(todos),
                                    .item = [](Todo& todo) -> ui::Spec { return ui::Text{.text = ui::bind(todo.title)}; }}};
            CHECK(todos.observer_count() == 1);
            CHECK(todos[0].title.observer_count() == 1);
        }
        CHECK(todos.observer_count() == 0);
        CHECK(todos[0].title.observer_count() == 0);
        todos.emplace_back("b"); // nobody is listening
    }
    SECTION("the collection first")
    {
        ui::Control<ui::ForEach<Todo>> rows;
        std::unique_ptr<Hosted>        host;
        {
            ObservableVector<Todo> todos;
            todos.emplace_back("a");
            todos.emplace_back("b");
            host = std::make_unique<Hosted>(ui::ForEach{
                .items   = ui::bind(todos),
                .control = rows,
                .item    = [](Todo& todo) -> ui::Spec { return ui::Text{.text = ui::bind(todo.title)}; },
            });
            CHECK(rows->realized_count() == 2);
        }
        // The rows went with their items; the repeater is empty and still lays out.
        CHECK(rows->item_count() == 0);
        host->layout();
        CHECK(rows->realized_count() == 0);
    }
}

// ---- keyed ForEach over a Property ----

namespace
{
struct Song
{
    int         id = 0;
    std::string title;
    friend bool operator==(const Song&, const Song&) = default;
};

std::vector<Song> songs(int count)
{
    std::vector<Song> all;
    all.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        all.push_back({i, "Song " + std::to_string(i)});
    }
    return all;
}
} // namespace

TEST_CASE("a keyed ForEach re-seats its rows through a reorder", "[spec][dynamic][foreach][key]")
{
    Property<std::vector<Song>>    model{songs(5)};
    int                            built = 0;
    ui::Control<ui::ForEach<Song>> rows;
    Hosted                         host{ui::ForEach{
        .items   = ui::bind(model),
        .key     = &Song::id,
        .control = rows,
        .item    = [&built](const Song& song) -> ui::Spec
        {
            ++built;
            return ui::Text{.text = song.title, .height = 20.0f};
        },
    }};
    REQUIRE(built == 5);
    std::vector<const Element*> elements;
    elements.reserve(5);
    for (std::size_t i = 0; i < 5; ++i)
    {
        elements.push_back(rows->try_get_element(i));
    }

    // Reversed: every row is still the element built for its song.
    model.update([](std::vector<Song>& v) { std::reverse(v.begin(), v.end()); });
    host.layout();
    CHECK(built == 5);
    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK(rows->try_get_element(i) == elements[4 - i]);
    }
    CHECK(titles(*rows) == std::vector<std::string>{"Song 4", "Song 3", "Song 2", "Song 1", "Song 0"});

    // The first one to the end, one removed, one new, one renamed: two rows are built.
    model.update(
        [](std::vector<Song>& v)
        {
            std::rotate(v.begin(), v.begin() + 1, v.end()); // 3 2 1 0 4
            v.erase(v.begin() + 1);                         // 3 1 0 4
            v.insert(v.begin(), Song{9, "Song 9"});         // 9 3 1 0 4
            v[2].title = "Renamed";                         // song 1
        });
    host.layout();
    CHECK(built == 7);
    CHECK(titles(*rows) == std::vector<std::string>{"Song 9", "Song 3", "Renamed", "Song 0", "Song 4"});
    CHECK(rows->try_get_element(1) == elements[3]);
    CHECK(rows->try_get_element(3) == elements[0]);
    CHECK(rows->try_get_element(4) == elements[4]);
}

TEST_CASE("a keyed ForEach takes a callable for its key", "[spec][dynamic][foreach][key]")
{
    Property<std::vector<Song>>    model{songs(3)};
    ui::Control<ui::ForEach<Song>> rows;
    Hosted                         host{ui::ForEach<Song>{
        .items   = ui::bind(model),
        .key     = [](const Song& song) { return song.title; },
        .control = rows,
        .item    = [](const Song& song) -> ui::Spec { return ui::Text{.text = song.title, .height = 20.0f}; },
    }};
    const Element*                 last = rows->try_get_element(2);
    model.update([](std::vector<Song>& v) { std::swap(v[0], v[2]); });
    host.layout();
    CHECK(rows->try_get_element(0) == last);
}

TEST_CASE("a keyed ForEach follows any sequence of edits", "[spec][dynamic][foreach][key]")
{
    Property<std::vector<Song>>    model{songs(12)};
    ui::Control<ui::ForEach<Song>> rows;
    Hosted                         host{ui::ForEach{
        .items   = ui::bind(model),
        .key     = &Song::id,
        .control = rows,
        .item    = [](const Song& song) -> ui::Spec { return ui::Text{.text = song.title, .height = 20.0f}; },
    }};

    std::mt19937 random{28}; // NOLINT(bugprone-random-generator-seed): fixed, the same edits every run
    int          next_id = 100;
    for (int round = 0; round < 200; ++round)
    {
        std::map<int, std::pair<const Element*, std::string>> before;
        for (std::size_t i = 0; i < model.get().size(); ++i)
        {
            before[model.get()[i].id] = {rows->try_get_element(i), model.get()[i].title};
        }

        std::vector<Song> next = model.get();
        std::shuffle(next.begin(), next.end(), random);
        for (std::size_t n = random() % 4; n > 0 && !next.empty(); --n)
        {
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(random() % next.size()));
        }
        for (std::size_t n = random() % 4; n > 0; --n)
        {
            const int id = next_id++;
            next.insert(next.begin() + static_cast<std::ptrdiff_t>(random() % (next.size() + 1)),
                        Song{id, "Song " + std::to_string(id)});
        }
        if (!next.empty() && random() % 2 == 0)
        {
            next[random() % next.size()].title += "!";
        }

        model = next;
        host.layout();

        REQUIRE(rows->item_count() == next.size());
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            const Element* element = rows->try_get_element(i);
            REQUIRE(element != nullptr);
            REQUIRE(text_of(element) == next[i].title);
            const auto was = before.find(next[i].id);
            if (was != before.end() && was->second.second == next[i].title)
            {
                REQUIRE(element == was->second.first); // an unchanged item keeps its row
            }
        }
    }
}

// ---- the views follow an ObservableVector ----

TEST_CASE("the views follow an ObservableVector", "[spec][dynamic][collections]")
{
    ObservableVector<std::string> names{"a", "b"};
    const auto                    row = [](const std::string& name) -> ui::Spec { return ui::Text{.text = name}; };

    ui::Control<ui::ListView<std::string>> list;
    ui::Control<ui::TileView<std::string>> tiles;
    ui::Control<ui::ForEach<std::string>>  repeater;
    std::unique_ptr<Element>               tree   = ui::build(ui::VerticalStack{
        .content = {
            ui::ListView{.items = ui::bind(names), .control = list},
            ui::TileView{.items = ui::bind(names), .control = tiles},
            ui::ForEach{.items = ui::bind(names), .virtualize = true, .control = repeater, .item = row},
        }});
    const auto                             counts = [&]
    { return std::vector<std::size_t>{list->item_count(), tiles->item_count(), repeater->item_count()}; };
    CHECK(counts() == std::vector<std::size_t>(3, 2));

    names.push_back("c");
    names.insert(0, "first");
    CHECK(counts() == std::vector<std::size_t>(3, 4));
    names.move(0, 3);
    CHECK(counts() == std::vector<std::size_t>(3, 4));
    names.erase(1);
    names.replace(0, "A");
    CHECK(counts() == std::vector<std::size_t>(3, 3));
    names.clear();
    CHECK(counts() == std::vector<std::size_t>(3, 0));
    (void)measured(*tree);

    tree.reset();
    CHECK(names.observer_count() == 0);
}

TEST_CASE("a view over a keyed property keeps its selection on the item through a reorder",
          "[spec][dynamic][collections][key]")
{
    Property<std::vector<Song>> model{songs(6)};
    const auto                  row = [](const Song& song) -> ui::Spec { return ui::Text{.text = song.title}; };

    ui::Control<ui::ListView<Song>> list;
    ui::Control<ui::TileView<Song>> tiles;
    std::unique_ptr<Element>        tree = ui::build(ui::VerticalStack{
        .content = {
            ui::ListView{.items = ui::bind(model), .key = &Song::id, .control = list, .item = row},
            ui::TileView{.items = ui::bind(model), .key = &Song::id, .control = tiles, .item = row},
        }});
    (void)measured(*tree);
    list->set_selected_index(4);
    tiles->set_selected_index(4);

    // Song 4 is at index 1 once reversed, and still the selected one.
    model.update([](std::vector<Song>& v) { std::reverse(v.begin(), v.end()); });
    (void)measured(*tree);
    CHECK(list->selected_index() == 1);
    CHECK(tiles->selected_index() == 1);

    // Items removed and inserted around it shift it; removing it clears the selection.
    model.update(
        [](std::vector<Song>& v)
        {
            v.erase(v.begin());                     // song 5 goes: song 4 is first
            v.insert(v.begin(), Song{9, "Song 9"}); // and second again
            v.insert(v.begin(), Song{8, "Song 8"}); // and third
        });
    CHECK(list->item_count() == 7);
    CHECK(list->selected_index() == 2);
    CHECK(tiles->selected_index() == 2);

    model.update([](std::vector<Song>& v) { v.erase(v.begin() + 2); });
    CHECK(list->item_count() == 6);
    CHECK(list->selected_index() == std::nullopt);
    (void)measured(*tree);
}

TEST_CASE("a virtualized repeater over a keyed property shows the items in their new order",
          "[spec][dynamic][collections][key]")
{
    Property<std::vector<Song>>    model{songs(40)};
    ui::Control<ui::ForEach<Song>> rows;
    Hosted                         host{ui::ForEach{
        .items      = ui::bind(model),
        .key        = &Song::id,
        .virtualize = true,
        .control    = rows,
        .item       = [](const Song& song) -> ui::Spec { return ui::Text{.text = song.title, .height = 20.0f}; },
    }};
    REQUIRE(rows->realized_count() > 0);
    REQUIRE(rows->realized_count() < 40);

    std::mt19937 random{7}; // NOLINT(bugprone-random-generator-seed): fixed, so a failure reproduces
    for (int round = 0; round < 60; ++round)
    {
        std::vector<Song> next = model.get();
        std::shuffle(next.begin(), next.end(), random);
        if (round % 3 == 0 && !next.empty())
        {
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(random() % next.size()));
        }
        if (round % 4 == 0)
        {
            next.insert(next.begin() + static_cast<std::ptrdiff_t>(random() % (next.size() + 1)),
                        Song{1000 + round, "New " + std::to_string(round)});
        }
        model = next;
        host.layout();

        REQUIRE(rows->item_count() == next.size());
        for (const std::size_t index : rows->realized_indices())
        {
            REQUIRE(text_of(rows->try_get_element(index)) == next[index].title);
        }
    }
}

// ---- removal that animates ----

namespace
{
// How many draw calls a laid-out subtree makes.
std::size_t drawn(Element& root)
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    (void)root.measure({200.0f, 100.0f}, measurer, theme);
    root.arrange(Rect{0.0f, 0.0f, 200.0f, 100.0f});
    TestRenderer renderer;
    root.render(renderer, theme);
    return renderer.count();
}
} // namespace

TEST_CASE("a child removed from a stack with an AddRemoveTransition is seen leaving", "[stack][transition]")
{
    StackControl stack{Orientation::Vertical};
    stack.set_children_transitions({AddRemoveTransition{}});
    ButtonControl* leaving = stack.add(std::make_unique<ButtonControl>("Leaving"));
    stack.add(std::make_unique<TextControl>("Staying"));
    (void)drawn(stack);
    stack.tick(10000ms); // past the children's own entrance
    (void)drawn(stack);

    REQUIRE(stack.remove(leaving));
    // Gone from the tree at once: no child, no tab stop.
    CHECK(stack.children().size() == 1);
    std::vector<Element*> focusables;
    stack.collect_focusables(focusables);
    CHECK(focusables.empty());

    // Still drawn, as a recording, until its transition ends.
    const std::size_t leaving_drawn = drawn(stack);
    stack.tick(10000ms);
    const std::size_t settled = drawn(stack);
    CHECK(leaving_drawn > settled);
    CHECK(settled == 1);

    // Without the transition a removed child is simply gone.
    StackControl plain{Orientation::Vertical};
    TextControl* gone = plain.add(std::make_unique<TextControl>("Gone"));
    (void)drawn(plain);
    REQUIRE(plain.remove(gone));
    CHECK(drawn(plain) == 0);
}

TEST_CASE("the children a deletion moves wait for its exit, even with a RepositionTransition listed first",
          "[stack][transition]")
{
    StackControl stack{Orientation::Vertical};
    stack.set_children_transitions({RepositionTransition{}, AddRemoveTransition{}});
    const auto row = [](const char* text)
    {
        auto t = std::make_unique<TextControl>(text);
        t->set_height(20.0f);
        return t;
    };
    TextControl* leaving = stack.add(row("One"));
    TextControl* staying = stack.add(row("Two"));
    (void)drawn(stack);
    stack.tick(10000ms);
    (void)drawn(stack);

    REQUIRE(stack.remove(leaving));
    TextControl* added = stack.add(row("Three"));
    (void)drawn(stack);

    // 100 ms in, the exit is still playing: the survivor has not started into the gap yet.
    stack.tick(100ms);
    CHECK(staying->transition_target().translate.y == 20.0f);
    // Added in the same pass, the new row waits for the exit and the move before it fades in.
    stack.tick(500ms);
    CHECK(added->transition_target().opacity == 0.0f);
    CHECK(staying->transition_target().translate.y < 20.0f);

    stack.tick(10000ms);
    CHECK(staying->transition_target().translate.y == 0.0f);
    CHECK(added->transition_target().opacity == 1.0f);

    // A later move, with no deletion in its pass, is the RepositionTransition's again.
    stack.insert(0, row("Zero"));
    (void)drawn(stack);
    stack.tick(100ms);
    CHECK(staying->transition_target().translate.y < 0.0f);
    CHECK(staying->transition_target().translate.y > -20.0f);
}

TEST_CASE("a row removed from a ForEach with item transitions is seen leaving", "[spec][dynamic][foreach][transition]")
{
    ObservableVector<std::string>         names{"a", "b"};
    ui::Control<ui::ForEach<std::string>> rows;
    std::unique_ptr<Element>              tree = ui::build(ui::ForEach{
        .items            = ui::bind(names),
        .item_transitions = {ui::AddRemoveTransition{}},
        .control          = rows,
        .item             = [](const std::string& name) -> ui::Spec { return ui::Text{.text = name, .height = 20.0f}; },
    });
    (void)drawn(*tree);
    tree->tick(10000ms);
    REQUIRE(drawn(*tree) == 2);

    names.erase(0);
    CHECK(rows->item_count() == 1);
    const std::size_t leaving_drawn = drawn(*tree);
    CHECK(leaving_drawn > 1);
    tree->tick(10000ms);
    CHECK(drawn(*tree) == 1);
}

TEST_CASE("Match with a ContentTransition shows the old content leaving", "[spec][dynamic][match][transition]")
{
    Property<int>            page{0};
    std::unique_ptr<Element> tree = ui::build(ui::Match{
        .value               = ui::bind(page),
        .content_transitions = {ui::ContentTransition{}},
        .content             = [](const int& p) -> ui::Spec { return ui::Text{.text = "Page " + std::to_string(p)}; },
    });
    (void)drawn(*tree);
    tree->tick(10000ms);
    REQUIRE(drawn(*tree) == 1);

    page = 1;
    CHECK(drawn(*tree) > 1); // the new page coming in, the old one going out
    tree->tick(10000ms);
    CHECK(drawn(*tree) == 1);
}

TEST_CASE("clearing a stack with an AddRemoveTransition sees every child out", "[stack][transition]")
{
    StackControl stack{Orientation::Vertical};
    stack.set_children_transitions({AddRemoveTransition{}});
    stack.add(std::make_unique<TextControl>("One"));
    stack.add(std::make_unique<TextControl>("Two"));
    (void)drawn(stack);
    stack.tick(10000ms);
    REQUIRE(drawn(stack) == 2);

    stack.clear();
    CHECK(stack.children().empty());
    CHECK(drawn(stack) > 2); // both still drawn, each inside its fading layer
    stack.tick(10000ms);
    CHECK(drawn(stack) == 0);
}

TEST_CASE("a leaving child that hosts a native control is destroyed at once", "[stack][transition]")
{
    static int alive = 0;
    class NativeLeaf final : public Element
    {
    public:
        NativeLeaf()
        {
            ++alive;
        }
        ~NativeLeaf() override
        {
            --alive;
        }
        [[nodiscard]] bool automation_is_native_host() const override
        {
            return true;
        }

    protected:
        [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
        {
            return {40.0f, 20.0f};
        }
        void render_override(Renderer& renderer, const RenderContext&) const override
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, Color::from_rgba(0, 0, 0));
        }
    };

    alive = 0;
    StackControl stack{Orientation::Vertical};
    stack.set_children_transitions({AddRemoveTransition{}});
    // One level down, as a native control in a row would be.
    auto     row    = std::make_unique<StackControl>(Orientation::Horizontal);
    Element* nested = row.get();
    row->add(std::make_unique<NativeLeaf>());
    stack.add(std::move(row));
    (void)drawn(stack);
    stack.tick(10000ms);
    REQUIRE(alive == 1);

    REQUIRE(stack.remove(nested));
    CHECK(alive == 0);
    CHECK(drawn(stack) == 0);
}

// A hidden page's list is skipped by layout. What it was told meanwhile still has to reach it
// when the page shows again, through every container between them.
TEST_CASE("An item added to a list on a collapsed page is built once the page shows", "[spec][dynamic][foreach]")
{
    ObservableVector<Todo> todos;
    todos.emplace_back("a");
    Property<bool>                 shown{true};
    ui::Control<ui::ForEach<Todo>> rows;

    HeadlessUI ui = create_headless();
    ui.window().set_content(ui::VerticalStack{
        .content = {ui::VerticalStack{
            .visible = ui::bind(shown),
            .content = {ui::VerticalStack{
                .content = {ui::ForEach{
                    .items   = ui::bind(todos),
                    .control = rows,
                    .item    = [](Todo& todo) -> ui::Spec { return ui::Text{.text = ui::bind(todo.title)}; },
                }},
            }},
        }},
    });
    const auto frame = [&ui]
    {
        ui.layout({400.0f, 300.0f});
        (void)ui.window().take_damage();
        TestRenderer renderer;
        ui.window().render(renderer);
    };
    frame();
    REQUIRE(rows->realized_count() == 1);

    shown = false;
    frame();
    todos.emplace_back("b");
    frame(); // the frame that skips the hidden page and clears the window's flags
    shown = true;
    frame();
    CHECK(rows->item_count() == 2);
    CHECK(rows->realized_count() == 2);
    CHECK(titles(*rows) == std::vector<std::string>{"a", "b"});
}

// SPDX-License-Identifier: MIT
// Scenarios over ordinary controls: a wall of buttons, thousands of bound labels, a popup, typing.
#include "Bench.hpp"
#include "Fixture.hpp"

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <format>
#include <memory>
#include <string>
#include <vector>

namespace bench
{

namespace
{

constexpr int Buttons = 1000;

std::unique_ptr<ui::StackControl> button_wall(std::vector<ui::ButtonControl*>* buttons = nullptr)
{
    auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
    for (int i = 0; i < Buttons; ++i)
    {
        auto* button = stack->add(std::make_unique<ui::ButtonControl>("Button " + std::to_string(i)));
        if (buttons != nullptr)
        {
            buttons->push_back(button);
        }
    }
    return stack;
}

struct Wall
{
    explicit Wall(Backend backend) : fixture(backend)
    {
        fixture.window().set_content(button_wall(&buttons));
        fixture.frame();
    }
    Fixture                         fixture;
    std::vector<ui::ButtonControl*> buttons;
};

struct BoundLabels
{
    static constexpr int Count = 10'000;

    explicit BoundLabels(Backend backend) : values(Count), fixture(backend)
    {
        ui::SpecList labels;
        for (ui::Property<int>& value : values)
        {
            labels.nodes.push_back(
                ui::build(ui::Text{.text = ui::bind(value, [](int n) { return std::format("Value {}", n); })}));
        }
        fixture.window().set_content(ui::VerticalStack{.content = std::move(labels)});
        fixture.frame();
    }
    std::vector<ui::Property<int>> values;
    Fixture                        fixture;
};

struct PopupHost
{
    explicit PopupHost(Backend backend) : fixture(backend)
    {
        auto stack = button_wall();
        flyout     = stack->add(std::make_unique<ui::FlyoutControl>());
        (void)flyout->set_anchor(std::make_unique<ui::ButtonControl>("Open"));
        flyout->set_content(std::make_unique<ui::TextControl>("A flyout with a line of text"));
        fixture.window().set_content(std::move(stack));
        fixture.frame();
    }
    Fixture            fixture;
    ui::FlyoutControl* flyout = nullptr;
};

struct Typing
{
    explicit Typing(Backend backend) : fixture(backend)
    {
        std::string text;
        for (int i = 0; text.size() < 10'000; ++i)
        {
            text += "word" + std::to_string(i) + (i % 12 == 11 ? "\n" : " ");
        }
        auto owned = std::make_unique<ui::TextBoxControl>(std::move(text));
        owned->set_accepts_return(true);
        owned->set_wrapping(ui::TextWrapping::Wrap);
        box = fixture.window().set_content(std::move(owned));
        fixture.frame();
        (void)fixture.window().focus_element(box);
        fixture.frame();
    }
    Fixture             fixture;
    ui::TextBoxControl* box = nullptr;
};

} // namespace

void controls(Runner& runner)
{
    for (const Backend backend : {Backend::Headless, Backend::D2D})
    {
        if (runner.wants("buttons/create"))
        {
            runner.run(
                "buttons/create, lay out and render 1000",
                name(backend),
                [backend] { return std::make_unique<Fixture>(backend); },
                [](Fixture& f, Result&)
                {
                    f.window().set_content(button_wall());
                    f.frame();
                });
        }
        if (runner.wants("buttons/hover"))
        {
            runner.run(
                "buttons/hover 100 of 1000",
                name(backend),
                [backend] { return std::make_unique<Wall>(backend); },
                [](Wall& w, Result&)
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        const ui::Rect& b = w.buttons[static_cast<std::size_t>(i % 12)]->bounds();
                        ui::testing::seams(w.fixture.window())
                            .handle_pointer({{b.x + b.width * 0.5f, b.y + b.height * 0.5f},
                                             ui::PointerButton::None,
                                             ui::PointerAction::Move});
                        w.fixture.frame();
                    }
                });
        }
        if (runner.wants("properties"))
        {
            runner.run(
                "properties/update 10'000 bound labels",
                name(backend),
                [backend] { return std::make_unique<BoundLabels>(backend); },
                [](BoundLabels& b, Result&)
                {
                    for (ui::Property<int>& value : b.values)
                    {
                        ++value;
                    }
                    b.fixture.frame();
                });
        }
        if (runner.wants("popup"))
        {
            runner.run(
                "popup/open and close 50 times",
                name(backend),
                [backend] { return std::make_unique<PopupHost>(backend); },
                [](PopupHost& p, Result&)
                {
                    for (int i = 0; i < 50; ++i)
                    {
                        p.flyout->show();
                        p.fixture.frame();
                        p.flyout->hide();
                        p.fixture.frame();
                    }
                });
        }
        if (runner.wants("textbox"))
        {
            runner.run(
                "textbox/type 100 characters into 10'000",
                name(backend),
                [backend] { return std::make_unique<Typing>(backend); },
                [](Typing& t, Result&)
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        ui::testing::seams(t.fixture.window()).handle_text("x");
                        t.fixture.frame();
                    }
                });
        }
    }
}

} // namespace bench

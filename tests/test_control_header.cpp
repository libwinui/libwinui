// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/RadioButtons.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/PasswordBox.hpp"
#include "winui/ui/text/RichEditBox.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
struct Case
{
    const char*                                name;
    float                                      gap; // the gap between the header and the control
    std::function<std::unique_ptr<Element>()>  make;
    std::function<void(Element&, std::string)> set_header;
};

template <typename T>
Case make_case(const char* name, float gap, const std::function<std::unique_ptr<T>()>& make)
{
    return Case{name,
                gap,
                [make] { return std::unique_ptr<Element>(make()); },
                [](Element& e, std::string text) { static_cast<T&>(e).set_header(std::move(text)); }};
}

std::vector<Case> cases()
{
    return {
        make_case<SliderControl>("Slider", 4.0f, [] { return std::make_unique<SliderControl>(); }),
        make_case<ToggleSwitchControl>("ToggleSwitch", 4.0f, [] { return std::make_unique<ToggleSwitchControl>(); }),
        make_case<ComboBoxControl>("ComboBox",
                                   8.0f,
                                   [] { return std::make_unique<ComboBoxControl>(std::vector<std::string>{"A"}); }),
        make_case<ComboBoxControl>("editable ComboBox",
                                   8.0f,
                                   []
                                   {
                                       auto c = std::make_unique<ComboBoxControl>(std::vector<std::string>{"A"});
                                       c->set_editable(true);
                                       return c;
                                   }),
        make_case<TextBoxControl>("TextBox", 8.0f, [] { return std::make_unique<TextBoxControl>(); }),
        make_case<NumberBoxControl>("NumberBox", 8.0f, [] { return std::make_unique<NumberBoxControl>(); }),
        make_case<AutoSuggestBoxControl>("AutoSuggestBox", 8.0f, [] { return std::make_unique<AutoSuggestBoxControl>(); }),
        make_case<RadioButtonsControl>("RadioButtons",
                                       8.0f,
                                       []
                                       {
                                           return std::make_unique<RadioButtonsControl>(
                                               std::vector<std::string>{"A", "B"});
                                       }),
    };
}
} // namespace

TEST_CASE("Every input control stacks its header above itself", "[header][layout]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    const float          line  = measurer.measure_wrapped("Header", theme.text_style, 300.0f).height;

    for (const Case& c : cases())
    {
        CAPTURE(c.name);
        // A control's own minimum height covers its header too, as set_min_height() does
        // (the 32 DIP minimum on TextBox), so it is lifted here to compare natural sizes.
        const auto lift = [](Element& e)
        {
            e.set_min_height(0.0f);
            if (auto* box = dynamic_cast<TextBoxControl*>(&e); box != nullptr)
            {
                box->set_uses_theme_min_height(false);
            }
        };
        auto plain = c.make();
        lift(*plain);
        const Size bare = plain->measure({300.0f, Unbounded}, measurer, theme);
        plain->arrange({0.0f, 0.0f, bare.width, bare.height});

        auto headed = c.make();
        lift(*headed);
        c.set_header(*headed, "Header");
        const Size size = headed->measure({300.0f, Unbounded}, measurer, theme);
        CHECK_THAT(size.height, WithinAbs(bare.height + line + c.gap, 1e-3));
        headed->arrange({0.0f, 0.0f, size.width, size.height});
        CHECK_THAT(headed->bounds().y, WithinAbs(plain->bounds().y + line + c.gap, 1e-3));
        CHECK_THAT(headed->bounds().height, WithinAbs(plain->bounds().height, 1e-3));
        CHECK(headed->automation_name() == "Header");

        TestRenderer renderer;
        headed->render(renderer, theme);
        REQUIRE_FALSE(renderer.commands.empty());
        const auto* first = std::get_if<DrawTextRun>(&renderer.commands.front());
        REQUIRE(first != nullptr);
        CHECK(first->text == "Header");
        CHECK(first->color == theme.colors().text);

        c.set_header(*headed, "");
        CHECK_THAT(headed->measure({300.0f, Unbounded}, measurer, theme).height, WithinAbs(bare.height, 1e-3));
    }
}

TEST_CASE("A header wraps at the width the control is given", "[header][layout][wrap]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    TextBoxControl box;
    box.set_header("A  header long enough to need a second line");
    const float wide   = box.measure({1000.0f, Unbounded}, measurer, theme).height;
    const float narrow = box.measure({120.0f, Unbounded}, measurer, theme).height;
    CHECK(narrow > wide);
}

TEST_CASE("A disabled control grays its header", "[header][render]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    ComboBoxControl combo;
    combo.set_header("Colors");
    combo.set_enabled(false);
    const Size size = combo.measure({300.0f, Unbounded}, measurer, theme);
    combo.arrange({0.0f, 0.0f, size.width, size.height});
    TestRenderer renderer;
    combo.render(renderer, theme);
    CHECK(std::get<DrawTextRun>(renderer.commands.front()).color == theme.colors().text_disabled);
}

TEST_CASE("Every input spec binds its header", "[header][spec][binding]")
{
    Property<std::string> header{"First"};

    ui::Control<ui::Slider>         slider;
    ui::Control<ui::ToggleSwitch>   toggle;
    ui::Control<ui::ComboBox>       combo;
    ui::Control<ui::ComboBox>       editable;
    ui::Control<ui::TextBox>        text;
    ui::Control<ui::NumberBox>      number;
    ui::Control<ui::AutoSuggestBox> suggest;

    std::vector<std::unique_ptr<Element>> trees;
    trees.push_back(ui::build(ui::Slider{.header = bind(header), .control = slider}));
    trees.push_back(ui::build(ui::ToggleSwitch{.header = bind(header), .control = toggle}));
    trees.push_back(ui::build(ui::ComboBox{.header = bind(header), .control = combo}));
    trees.push_back(ui::build(ui::ComboBox{.header = bind(header), .editable = true, .control = editable}));
    trees.push_back(ui::build(ui::TextBox{.header = bind(header), .control = text}));
    trees.push_back(ui::build(ui::NumberBox{.header = bind(header), .control = number}));
    trees.push_back(ui::build(ui::AutoSuggestBox{.header = bind(header), .control = suggest}));

    header = "Second";
    CHECK(slider->header() == "Second");
    CHECK(toggle->header() == "Second");
    CHECK(combo->header() == "Second");
    CHECK(editable->header() == "Second");
    CHECK(text->header() == "Second");
    CHECK(number->header() == "Second");
    CHECK(suggest->header() == "Second");
}

namespace
{
struct DescriptionCase
{
    const char*                                name;
    std::function<std::unique_ptr<Element>()>  make;
    std::function<void(Element&, std::string)> set_description;
};

template <typename T>
DescriptionCase description_case(const char* name)
{
    return DescriptionCase{name,
                           [] { return std::unique_ptr<Element>(std::make_unique<T>()); },
                           [](Element& e, std::string text) { static_cast<T&>(e).set_description(std::move(text)); }};
}
} // namespace

TEST_CASE("Every text input stacks its description under itself", "[header][description][layout]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    const float          line  = measurer.measure_wrapped("Help", theme.text_style, 300.0f).height;

    for (const DescriptionCase& c :
         {description_case<TextBoxControl>("TextBox"),
          description_case<PasswordBoxControl>("PasswordBox"),
          description_case<NumberBoxControl>("NumberBox"),
          description_case<AutoSuggestBoxControl>("AutoSuggestBox"),
          description_case<RichEditBoxControl>("RichEditBox"),
          description_case<ComboBoxControl>("ComboBox"),
          DescriptionCase{"editable ComboBox",
                          []
                          {
                              auto c = std::make_unique<ComboBoxControl>();
                              c->set_editable(true);
                              return std::unique_ptr<Element>(std::move(c));
                          },
                          [](Element& e, std::string text)
                          { static_cast<ComboBoxControl&>(e).set_description(std::move(text)); }}})
    {
        CAPTURE(c.name);
        auto       plain = c.make();
        const Size bare  = plain->measure({300.0f, Unbounded}, measurer, theme);
        plain->arrange({0.0f, 0.0f, bare.width, bare.height});

        auto described = c.make();
        c.set_description(*described, "Help");
        const Size size = described->measure({300.0f, Unbounded}, measurer, theme);
        CHECK_THAT(size.height, WithinAbs(bare.height + line, 1e-3));
        described->arrange({0.0f, 0.0f, size.width, size.height});
        if (dynamic_cast<PasswordBoxControl*>(described.get()) == nullptr) // its bounds wrap its box's
        {
            CHECK_THAT(described->bounds().y, WithinAbs(plain->bounds().y, 1e-3));
            CHECK_THAT(described->bounds().height, WithinAbs(plain->bounds().height, 1e-3));
        }
        CHECK(described->automation_help_text() == "Help");

        TestRenderer renderer;
        described->render(renderer, theme);
        bool drawn = false;
        for (const RenderCommand& command : renderer.commands)
        {
            if (const auto* run = std::get_if<DrawTextRun>(&command); run != nullptr && run->text == "Help")
            {
                drawn = true;
                CHECK(run->color == theme.palette.text_fill_secondary);
                CHECK(run->bounds.y >= size.height - line - 3.0f - 1e-3f); // under the field, inside a focus ring's outset
                CHECK(run->bounds.bottom() <= size.height + 1e-3f);
            }
        }
        CHECK(drawn);

        c.set_description(*described, "");
        CHECK_THAT(described->measure({300.0f, Unbounded}, measurer, theme).height, WithinAbs(bare.height, 1e-3));
    }
}

TEST_CASE("An explicit accessibility description wins over the visible one", "[header][description][automation]")
{
    TextBoxControl box;
    box.set_description("Visible help");
    CHECK(box.automation_help_text() == "Visible help");
    box.set_accessibility({.description = "Spoken help"});
    CHECK(box.automation_help_text() == "Spoken help");
}

TEST_CASE("Every input spec binds its description", "[header][description][spec][binding]")
{
    Property<std::string> description{"First"};

    ui::Control<ui::TextBox>        text;
    ui::Control<ui::PasswordBox>    password;
    ui::Control<ui::NumberBox>      number;
    ui::Control<ui::AutoSuggestBox> suggest;
    ui::Control<ui::RichEditBox>    rich;
    ui::Control<ui::ComboBox>       combo;
    ui::Control<ui::ComboBox>       editable;

    std::vector<std::unique_ptr<Element>> trees;
    trees.push_back(ui::build(ui::TextBox{.description = bind(description), .control = text}));
    trees.push_back(ui::build(ui::PasswordBox{.description = bind(description), .control = password}));
    trees.push_back(ui::build(ui::NumberBox{.description = bind(description), .control = number}));
    trees.push_back(ui::build(ui::AutoSuggestBox{.description = bind(description), .control = suggest}));
    trees.push_back(ui::build(ui::RichEditBox{.description = bind(description), .control = rich}));
    trees.push_back(ui::build(ui::ComboBox{.description = bind(description), .control = combo}));
    trees.push_back(ui::build(ui::ComboBox{.description = bind(description), .editable = true, .control = editable}));

    description = "Second";
    CHECK(text->description() == "Second");
    CHECK(password->description() == "Second");
    CHECK(number->description() == "Second");
    CHECK(suggest->description() == "Second");
    CHECK(rich->description() == "Second");
    CHECK(combo->description() == "Second");
    CHECK(editable->description() == "Second");
}

namespace
{
struct ErrorCase
{
    const char*                                name;
    std::function<std::unique_ptr<Element>()>  make;
    std::function<void(Element&, std::string)> set_description;
    std::function<void(Element&, std::string)> set_error;
};

template <typename T>
ErrorCase error_case(const char* name)
{
    return ErrorCase{name,
                     [] { return std::unique_ptr<Element>(std::make_unique<T>()); },
                     [](Element& e, std::string text) { static_cast<T&>(e).set_description(std::move(text)); },
                     [](Element& e, std::string text) { static_cast<T&>(e).set_error(std::move(text)); }};
}

bool draws_text(const TestRenderer& renderer, std::string_view text, const Color* color = nullptr)
{
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command); run != nullptr && run->text == text)
        {
            return color == nullptr || run->color == *color;
        }
    }
    return false;
}

bool strokes_in(const TestRenderer& renderer, Color color)
{
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command); stroke != nullptr && stroke->color == color)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("Every text input shows its error in its description's place, with a critical border",
          "[header][description][error]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme    = Theme::light();
    const Color          critical = theme.colors().critical;

    for (const ErrorCase& c :
         {error_case<TextBoxControl>("TextBox"),
          error_case<PasswordBoxControl>("PasswordBox"),
          error_case<NumberBoxControl>("NumberBox"),
          error_case<AutoSuggestBoxControl>("AutoSuggestBox"),
          error_case<RichEditBoxControl>("RichEditBox"),
          error_case<ComboBoxControl>("ComboBox")})
    {
        CAPTURE(c.name);
        auto field = c.make();
        c.set_description(*field, "Help");
        c.set_error(*field, "Wrong");
        const Size size = field->measure({300.0f, Unbounded}, measurer, theme);
        field->arrange({0.0f, 0.0f, size.width, size.height});

        TestRenderer renderer;
        field->render(renderer, theme);
        CHECK(draws_text(renderer, "Wrong", &critical));
        CHECK_FALSE(draws_text(renderer, "Help"));
        CHECK(strokes_in(renderer, critical));
        CHECK(field->automation_help_text() == "Wrong");
        CHECK_FALSE(field->automation_valid());

        c.set_error(*field, "");
        (void)field->measure({300.0f, Unbounded}, measurer, theme);
        field->arrange({0.0f, 0.0f, size.width, size.height});
        TestRenderer cleared;
        field->render(cleared, theme);
        CHECK(draws_text(cleared, "Help"));
        CHECK_FALSE(strokes_in(cleared, critical));
        CHECK(field->automation_valid());
    }
}

TEST_CASE("An input spec's error follows its binding", "[header][error][spec][binding]")
{
    Property<std::string>    email{""};
    Property<bool>           tried{false};
    ui::Control<ui::TextBox> box;
    std::unique_ptr<Element> tree = ui::build(ui::TextBox{
        .header  = "Email",
        .error   = ui::bind([](const std::string& e, bool t)
                            { return t && e.find('@') == std::string::npos ? "Enter an email address." : ""; },
                            email,
                            tried),
        .text    = email,
        .control = box,
    });
    CHECK(box->error().empty()); // nothing tried yet
    tried = true;
    CHECK(box->error() == "Enter an email address.");
    email = "ada@example.com";
    CHECK(box->error().empty());
}

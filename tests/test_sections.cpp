// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/navigation/Sections.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
void lay_out(Element& e, const TextMeasurer& m, const Theme& theme, Size size)
{
    (void)e.measure(size, m, theme);
    e.arrange({0, 0, size.width, size.height});
}

std::unique_ptr<SectionsControl> email_sections()
{
    auto sections = std::make_unique<SectionsControl>();
    sections->set_title("EMAIL");
    for (const char* h : {"All", "Unread", "Flagged", "Urgent"})
    {
        sections->add_item(h, std::make_unique<TextControl>(std::string(h) + " emails go here."));
    }
    return sections;
}

TextStyle header_style(const Theme& theme)
{
    TextStyle s = theme.text_style;
    s.font_size = 24.0f;
    s.weight    = FontWeight::SemiLight;
    return s;
}
} // namespace

TEST_CASE("Sections selects its first item and lays out the title, the header row and the content", "[sections][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme    = Theme::light();
    auto                 sections = email_sections();
    CHECK(sections->selected_index() == 0);
    CHECK(sections->header_carousel_enabled());
    lay_out(*sections, m, theme, {800.0f, 400.0f});
    const Point o{sections->bounds().x, sections->bounds().y};

    // 12,14,0,13 of padding around the 14px Bold title.
    TextStyle title = theme.text_style;
    title.font_size = 14.0f;
    title.weight    = FontWeight::Bold;
    const float th  = m.measure("EMAIL", title).height;
    CHECK_THAT(sections->header_row().y - o.y, WithinAbs(14.0f + th + 13.0f, 1e-3));
    CHECK_THAT(sections->header_row().height, WithinAbs(48.0f, 1e-3));
    // The content's 12,0,12,0 margin.
    CHECK_THAT(sections->content_bounds().x - o.x, WithinAbs(12.0f, 1e-3));
    CHECK_THAT(sections->content_bounds().y, WithinAbs(sections->header_row().bottom(), 1e-3));

    // Headers: 12 either side of the 24px SemiLight text, in order.
    CHECK(sections->static_headers());
    const Rect all    = *sections->header_bounds(0);
    const Rect unread = *sections->header_bounds(1);
    CHECK_THAT(all.width, WithinAbs(std::ceil(m.measure("All", header_style(theme)).width) + 24.0f, 1e-3));
    CHECK_THAT(unread.x, WithinAbs(all.right(), 1e-3));

    TestRenderer r;
    sections->render(r, theme);
    // The selected pipe: 3 tall, 2 above the row's bottom, as wide as the text, in the accent.
    bool pipe = false;
    for (const auto& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == theme.accent_button_normal.background)
        {
            pipe = true;
            CHECK_THAT(f->bounds.height, WithinAbs(3.0f, 1e-3));
            CHECK_THAT(f->bounds.bottom(), WithinAbs(sections->header_row().bottom() - 2.0f, 1e-3));
            CHECK_THAT(f->bounds.x, WithinAbs(all.x + 12.0f, 1e-3));
            CHECK_THAT(f->radius, WithinAbs(1.5f, 1e-3));
        }
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == "Unread")
        {
            // Unselected: text_60.
            CHECK_THAT(t->color.a, WithinAbs(theme.colors().text.a * 0.6f, 1e-3));
            CHECK(t->style.font_size == 24.0f);
        }
    }
    CHECK(pipe);
}

TEST_CASE("Headers that do not fit rotate so the selected one leads, and only then does the keyboard wrap",
          "[sections][layout][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme    = Theme::light();
    auto                 sections = email_sections();
    lay_out(*sections, m, theme, {800.0f, 400.0f});
    ui::testing::set_focused(*sections, true);

    sections->handle_key({Key::ArrowLeft, KeyAction::Down});
    CHECK(sections->selected_index() == 0);
    sections->handle_key({Key::End, KeyAction::Down});
    sections->handle_key({Key::ArrowRight, KeyAction::Down});
    CHECK(sections->selected_index() == 3);

    // Too narrow for all four: the carousel. The selected header is first, the rest follow it.
    lay_out(*sections, m, theme, {200.0f, 400.0f});
    CHECK_FALSE(sections->static_headers());
    CHECK_THAT(sections->header_bounds(3)->x, WithinAbs(sections->header_row().x, 1e-3));
    sections->handle_key({Key::ArrowRight, KeyAction::Down}); // wraps
    CHECK(sections->selected_index() == 0);
    lay_out(*sections, m, theme, {200.0f, 400.0f});
    CHECK_THAT(sections->header_bounds(0)->x, WithinAbs(sections->header_row().x, 1e-3));
    sections->handle_key({Key::ArrowLeft, KeyAction::Down});
    CHECK(sections->selected_index() == 3);

    // Ctrl+Tab and Ctrl+PageUp work from anywhere inside.
    CHECK(sections->handle_shortcut({Key::Tab, KeyAction::Down, {.ctrl = true}}));
    CHECK(sections->selected_index() == 0);
    CHECK(sections->handle_shortcut({Key::PageUp, KeyAction::Down, {.ctrl = true}}));
    CHECK(sections->selected_index() == 3);

    sections->set_header_carousel_enabled(false);
    lay_out(*sections, m, theme, {200.0f, 400.0f});
    CHECK(sections->static_headers());
    REQUIRE(sections->header_bounds(3).has_value());
    CHECK(sections->header_bounds(3)->right() <= sections->header_row().right() + 1e-3f);
}

TEST_CASE("A header click selects it, and hovering the row shows the navigation buttons", "[sections][input]")
{
    HeadlessTextMeasurer       m;
    const Theme                theme    = Theme::light();
    auto                       sections = email_sections();
    std::optional<std::size_t> last;
    sections->on_selection_changed([&](std::size_t i) { last = i; });
    lay_out(*sections, m, theme, {800.0f, 400.0f});

    sections->handle_pointer({sections->header_bounds(2)->center(), PointerButton::Left, PointerAction::Down});
    sections->handle_pointer({sections->header_bounds(2)->center(), PointerButton::Left, PointerAction::Up});
    CHECK(sections->selected_index() == 2);
    CHECK(last == 2);

    CHECK_FALSE(sections->next_button_bounds().has_value());
    sections->handle_pointer({sections->header_row().center(), PointerButton::None, PointerAction::Move});
    REQUIRE(sections->next_button_bounds().has_value());
    REQUIRE(sections->previous_button_bounds().has_value());
    // 20x36 at the row's ends, 6 down.
    CHECK_THAT(sections->next_button_bounds()->right(), WithinAbs(sections->header_row().right(), 1e-3));
    CHECK_THAT(sections->next_button_bounds()->y - sections->header_row().y, WithinAbs(6.0f, 1e-3));
    CHECK_THAT(sections->next_button_bounds()->width, WithinAbs(20.0f, 1e-3));
    const Point next = sections->next_button_bounds()->center();
    sections->handle_pointer({next, PointerButton::None, PointerAction::Move});
    sections->handle_pointer({next, PointerButton::Left, PointerAction::Down});
    sections->handle_pointer({next, PointerButton::Left, PointerAction::Up});
    CHECK(sections->selected_index() == 3);
    CHECK_FALSE(sections->next_button_bounds().has_value());

    sections->handle_pointer({Point{0.0f, 390.0f}, PointerButton::None, PointerAction::Leave});
    CHECK_FALSE(sections->previous_button_bounds().has_value());
}

TEST_CASE("Changing the selection flies the old content out and the new one in", "[sections][animation]")
{
    HeadlessTextMeasurer m;
    const Theme          theme    = Theme::light();
    auto                 sections = email_sections();
    lay_out(*sections, m, theme, {800.0f, 400.0f});
    sections->tick(0ms); // a frame clock drives it

    const auto frame = [&]
    {
        TestRenderer r;
        sections->render(r, theme);
        std::string shown;
        float       opacity = 1.0f;
        float       offset  = 0.0f;
        for (const auto& c : r.commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text.find("emails") != std::string::npos)
            {
                shown = t->text;
            }
            if (const auto* o = std::get_if<PushOpacityLayer>(&c))
            {
                opacity = o->opacity;
            }
            if (const auto* x = std::get_if<PushTransform>(&c))
            {
                offset = x->offset.x;
            }
        }
        return std::tuple{shown, opacity, offset};
    };

    sections->set_selected_index(1);
    lay_out(*sections, m, theme, {800.0f, 400.0f});
    sections->tick(40ms);
    {
        const auto [shown, opacity, offset] = frame();
        CHECK(shown == "All emails go here.");
        CHECK(opacity < 1.0f);
        CHECK(offset < 0.0f);
    }
    sections->tick(100ms);
    {
        const auto [shown, opacity, offset] = frame();
        CHECK(shown == "Unread emails go here.");
        CHECK(offset > 0.0f);
        CHECK(offset < 20.0f);
    }
    sections->tick(800ms);
    {
        const auto [shown, opacity, offset] = frame();
        CHECK(shown == "Unread emails go here.");
        CHECK(opacity == 1.0f);
        CHECK(offset == 0.0f);
    }
}

TEST_CASE("ui::Sections builds a Sections control with its title and items", "[sections][spec]")
{
    ui::Control<ui::Sections> sections;
    auto built = ui::Sections{.title   = "EMAIL",
                              .control = sections,
                              .items =
                                  {
                                      ui::Section{.header = "All", .content = ui::Text{.text = "all emails go here."}},
                                      ui::Section{.header = "Unread", .content = ui::Text{.text = "unread emails go here."}},
                                  }}
                     .build();
    REQUIRE(sections != nullptr);
    CHECK(sections->title() == "EMAIL");
    CHECK(sections->item_count() == 2);
    CHECK(sections->header(1) == "Unread");
}

namespace
{
void click(HeadlessUI& ui, Point p)
{
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("Sections: a press in the selected item focuses the control there", "[sections][focus]")
{
    HeadlessUI       ui;
    auto             sections = std::make_unique<SectionsControl>();
    SectionsControl* view     = sections.get();
    TextBoxControl*  box      = view->add_item("Mail", std::make_unique<TextBoxControl>());
    (void)view->add_item("Calendar", std::make_unique<TextControl>("Calendar"));
    ui.window().set_content(std::move(sections));
    ui.layout({800.0f, 600.0f});

    const Point on_box{box->bounds().x + 10.0f, box->bounds().center().y};
    CHECK_FALSE(view->takes_focus_at(on_box));
    click(ui, on_box);
    CHECK(ui.window().focused() == box);

    CHECK(view->takes_focus_at(view->header_row().center()));
}

// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/TestPrintHost.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Printing.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdio>

using namespace ui;
using namespace ui::testing;

namespace
{
// A job of `lines` numbered lines, 40 a page, an inch in from the edges.
PrintJob numbered_lines(std::size_t lines, std::vector<PrintPage>* seen = nullptr)
{
    return PrintJob{
        .title      = "Lines",
        .page_count = [lines](const PrintPage&, const PrintContext&) { return (lines + 39) / 40; },
        .draw_page =
            [lines, seen](Renderer& renderer, const PrintPage& page, const PrintContext& context)
        {
            if (seen != nullptr)
            {
                seen->push_back(page);
            }
            TextStyle style  = context.theme.text_style;
            style.horizontal = TextAlign::Leading;
            for (std::size_t i = 0; i < 40 && page.index * 40 + i < lines; ++i)
            {
                renderer.draw_text("Line " + std::to_string(page.index * 40 + i + 1),
                                   Rect{96.0f, 96.0f + 20.0f * static_cast<float>(i), page.size.width - 192.0f, 20.0f},
                                   colors::Black,
                                   style);
            }
        },
    };
}

std::vector<std::string> texts(const TestRenderer& page)
{
    std::vector<std::string> out;
    for (const RenderCommand& c : page.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            out.push_back(t->text);
        }
    }
    return out;
}
} // namespace

TEST_CASE("print() draws every page of the job on the host's paper", "[printing]")
{
    TestPrintHost printer;
    set_print_host(&printer);
    Window                 window;
    std::vector<PrintPage> seen;

    REQUIRE(print(window, numbered_lines(90, &seen)) == PrintResult::Printed);
    REQUIRE(printer.pages.size() == 3);
    CHECK(printer.titles == std::vector<std::string>{"Lines"});
    CHECK(printer.owners == std::vector<const Window*>{&window});
    CHECK(texts(printer.pages[0]).front() == "Line 1");
    CHECK(texts(printer.pages[2]).back() == "Line 90");
    REQUIRE(seen.size() == 3);
    CHECK(seen[1].index == 1);
    CHECK(seen[1].size == Size{816.0f, 1056.0f});
    CHECK(seen[1].printable.x == 24.0f);
}

TEST_CASE("print() draws only the pages the user chose, and nothing when canceled", "[printing]")
{
    TestPrintHost printer;
    set_print_host(&printer);

    printer.chosen = std::vector<std::size_t>{1, 7}; // page 8 doesn't exist
    REQUIRE(print(numbered_lines(90)) == PrintResult::Printed);
    CHECK(printer.indices == std::vector<std::size_t>{1});
    CHECK(printer.owners == std::vector<const Window*>{nullptr});

    printer.answer = PrintResult::Canceled;
    CHECK(print(numbered_lines(90)) == PrintResult::Canceled);
    CHECK(printer.pages.size() == 1);

    // A job without its callbacks, or without pages, prints nothing.
    printer.answer = PrintResult::Printed;
    CHECK(print(PrintJob{.title = "Empty"}) == PrintResult::Canceled);
    CHECK(print(numbered_lines(0)) == PrintResult::Canceled);
}

TEST_CASE("draw_element() lays a control out on the page and draws it", "[printing]")
{
    TestPrintHost printer;
    set_print_host(&printer);
    const PrintResult result = print(PrintJob{
        .title      = "Hello",
        .page_count = [](const PrintPage&, const PrintContext&) { return std::size_t{1}; },
        .draw_page =
            [](Renderer& renderer, const PrintPage& page, const PrintContext& context)
        {
            std::unique_ptr<Element> text = build(ui::Text{.text = "Hello on paper"});
            draw_element(renderer, *text, page.printable, context);
            CHECK(text->bounds().x == page.printable.x);
        },
    });
    REQUIRE(result == PrintResult::Printed);
    CHECK(texts(printer.pages[0]) == std::vector<std::string>{"Hello on paper"});
}

// Prints to a real printer driver: run by hand with "[.print]". Writes printing-check.pdf into
// the working directory through Microsoft Print to PDF, no dialog.
TEST_CASE("Print three pages to Microsoft Print to PDF", "[.print]")
{
    std::vector<PrintPage> seen;
    PrintJob               job = numbered_lines(90, &seen);
    job.printer                = "Microsoft Print to PDF";
    job.output_file            = std::filesystem::absolute("printing-check.pdf");
    auto draw_lines            = std::move(job.draw_page);
    job.draw_page =
        [draw = std::move(draw_lines)](Renderer& renderer, const PrintPage& page, const PrintContext& context) mutable
    {
        // A frame round the printable area, a heading, then the lines.
        renderer.stroke_rounded_rect(page.printable, 0.0f, colors::Black, 1.0f);
        std::unique_ptr<Element> heading = build(
            ui::Text{.text = "Page " + std::to_string(page.index + 1) + " of the libwinui print check",
                     .role = TextRole::Subtitle});
        draw_element(renderer, *heading, Rect{96.0f, 40.0f, page.size.width - 192.0f, 40.0f}, context);
        draw(renderer, page, context);
    };
    REQUIRE(print(std::move(job)) == PrintResult::Printed);
    REQUIRE(seen.size() == 3);
    CHECK(std::filesystem::file_size("printing-check.pdf") > 1000);
}

// The print dialog itself: run by hand with "[.print-dialog]" while a script picks the printer and
// the pages. Writes printing-dialog.pdf into the working directory.
TEST_CASE("Print through the dialog", "[.print-dialog]")
{
    std::vector<PrintPage> seen;
    PrintJob               job = numbered_lines(90, &seen);
    job.title                  = "libwinui dialog check";
    job.output_file            = std::filesystem::absolute("printing-dialog.pdf");
    const PrintResult result   = print(std::move(job));
    std::printf("result %d, pages drawn:", static_cast<int>(result));
    for (const PrintPage& page : seen)
    {
        std::printf(" %zu", page.index + 1);
    }
    std::printf("\n");
}

// The dialog with a preview: run by hand with "[.print-preview]" while a script prints from it.
TEST_CASE("Print through the dialog with a preview", "[.print-preview]")
{
    std::vector<PrintPage> seen;
    PrintJob               job = numbered_lines(90, &seen);
    job.title                  = "libwinui preview check";
    const PrintResult result   = print(std::move(job));
    std::printf("result %d, pages drawn: %zu\n", static_cast<int>(result), seen.size());
}

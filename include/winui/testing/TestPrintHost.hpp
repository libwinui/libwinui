// SPDX-License-Identifier: MIT
#pragma once

#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Printing.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>
#include <string>
#include <vector>

#include <cstddef>

namespace ui::testing
{

/// PrintHost test double: shows no dialog and prints to nothing. It runs the job on US Letter
/// paper with the headless measurer, keeps each page's drawing, and answers as told.
///
/// Install it with set_print_host(); ui::print() then reaches it. It uninstalls itself when
/// destroyed.
///
/// Example:
/// @code
/// ui::testing::TestPrintHost printer;
/// ui::testing::set_print_host(&printer);
/// const ui::PrintResult result = ui::print(window, ui::PrintJob{
///     .title      = "Report",
///     .page_count = [](const ui::PrintPage&, const ui::PrintContext&) { return std::size_t{3}; },
///     .draw_page  = [](ui::Renderer&, const ui::PrintPage&, const ui::PrintContext&) { /* draw the page */ },
/// });
/// REQUIRE(result == ui::PrintResult::Printed);
/// REQUIRE(printer.pages.size() == 3);
/// @endcode
class TestPrintHost final : public PrintHost
{
public:
    TestPrintHost() = default;
    ~TestPrintHost() override
    {
        if (ui::detail::installed_print_host() == this)
        {
            ui::detail::install_print_host(nullptr);
        }
    }
    TestPrintHost(const TestPrintHost&)            = delete;
    TestPrintHost& operator=(const TestPrintHost&) = delete;

    /// The answer: Printed draws the chosen pages, Canceled and Failed draw nothing.
    PrintResult answer = PrintResult::Printed;
    /// The sheet, US Letter (8.5 x 11 inches) by default, with a quarter inch the printer can't reach.
    Size  sheet{816.0f, 1056.0f};
    float unprintable = 24.0f;
    /// The pages the user picked in the print dialog (0-based); unset prints them all.
    std::optional<std::vector<std::size_t>> chosen{};

    /// Each job's title and owner, in order.
    std::vector<std::string>   titles;
    std::vector<const Window*> owners;
    /// The drawing of each printed page, in order, and its index in the document.
    std::vector<TestRenderer> pages;
    std::vector<std::size_t>  indices;

    PrintResult print(const Window* owner, PrintJob& job) override
    {
        titles.push_back(job.title);
        owners.push_back(owner);
        if (answer != PrintResult::Printed)
        {
            return answer;
        }
        const Theme        theme = Theme::default_light();
        const PrintContext context{measurer_, theme};
        PrintPage page{.index = 0,
                       .size  = sheet,
                       .printable = Rect{unprintable, unprintable, sheet.width - 2 * unprintable, sheet.height - 2 * unprintable}};
        const std::size_t count = job.page_count(page, context);
        if (count == 0)
        {
            return PrintResult::Canceled;
        }
        std::vector<std::size_t> which;
        if (chosen)
        {
            for (const std::size_t i : *chosen)
            {
                if (i < count)
                {
                    which.push_back(i);
                }
            }
        }
        else
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                which.push_back(i);
            }
        }
        for (const std::size_t i : which)
        {
            page.index = i;
            pages.emplace_back();
            job.draw_page(pages.back(), page, context);
            indices.push_back(i);
        }
        return PrintResult::Printed;
    }

private:
    HeadlessTextMeasurer measurer_;
};

/// Installs `host` as what ui::print() uses, or nullptr for the platform's own.
inline void set_print_host(PrintHost* host)
{
    ui::detail::install_print_host(host);
}

} // namespace ui::testing

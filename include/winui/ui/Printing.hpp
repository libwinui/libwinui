// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <cstddef>

namespace ui
{

class Element;
class Renderer;
class TextMeasurer;
struct Theme;
class Window;

/// One page of a print job, as its callbacks get it. Sizes are DIPs, 1/96 inch, as on screen.
struct PrintPage
{
    std::size_t index = 0;   ///< 0-based, in the whole document (not just the pages being printed).
    Size        size{};      ///< The sheet, in the orientation the user chose.
    Rect        printable{}; ///< The part of the sheet the printer can reach, from its top-left.
};

/// Which print dialog ui::print() shows.
enum class PrintDialog
{
    Preview, ///< The system print dialog with a live preview of the pages. The default.
    Classic, ///< The print dialog without a preview.
};

/// What a print job measures and draws with: a text measurer for the printer and the light
/// theme, since paper is white. Valid during the call.
struct PrintContext
{
    const TextMeasurer& measurer;
    const Theme&        theme;
};

/// A document to print: how many pages it has, and how to draw each.
///
/// Example:
/// @code
/// const std::vector<std::string> lines = {"Sales report", "Q3: 1240 units", "Q4: 1385 units"};
/// const ui::PrintResult result = ui::print(window, ui::PrintJob{
///     .title      = "Report",
///     .page_count = [&](const ui::PrintPage&, const ui::PrintContext&)
///     { return (lines.size() + 49) / 50; },
///     .draw_page  = [&](ui::Renderer& renderer, const ui::PrintPage& page, const ui::PrintContext& context)
///     {
///         // 50 lines a page, an inch from the edges.
///         ui::TextStyle style = context.theme.text_style;
///         style.horizontal    = ui::TextAlign::Leading;
///         for (std::size_t i = 0; i < 50 && page.index * 50 + i < lines.size(); ++i)
///         {
///             renderer.draw_text(lines[page.index * 50 + i],
///                                ui::Rect{96, 96 + 18.0f * i, page.size.width - 192, 18},
///                                ui::colors::Black,
///                                style);
///         }
///     },
/// });
/// @endcode
struct PrintJob
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The document's name in the print queue.
    std::string title{};
    /// How many pages the document has on sheets like `page` (index 0). Called before the dialog
    /// shows, for its page range, and again when the user picks other paper.
    Callback<std::size_t(const PrintPage& page, const PrintContext& context)> page_count{};
    /// Draws page `page.index`, from the sheet's top-left. Called for each page the user chose,
    /// in order, once per job (the printer makes the copies).
    Callback<void(Renderer& renderer, const PrintPage& page, const PrintContext& context)> draw_page{};
    /// A printer to print to without asking, by its name ("Microsoft Print to PDF"); unset shows
    /// the print dialog.
    std::optional<std::string> printer{};
    /// A file the printer's output goes to instead of the device, as "Print to file" does, such
    /// as a PDF from a PDF printer. Unset prints to the device. Setting it shows the classic
    /// dialog, since the preview dialog prints only to devices.
    std::optional<std::filesystem::path> output_file{};
    /// The dialog to ask with. The one with a preview by default. Where the system has no preview
    /// dialog, the classic one shows.
    PrintDialog dialog = PrintDialog::Preview;
};

/// How a print job ended.
enum class PrintResult
{
    Printed,  ///< Handed to the print spooler.
    Canceled, ///< The user closed the dialog without printing, or the document has no pages.
    Failed,   ///< The printer or the system refused it.
};

/// Shows the system print dialog over `owner`, where the user picks the printer, pages, copies and
/// paper and sees a live preview of the pages. Then draws the pages the user chose and hands them
/// to the printer as vector output, so text stays text. Blocks until the job is spooled, which is
/// quick compared to the printing itself. The app's windows keep handling input and painting
/// meanwhile.
PrintResult print(const Window& owner, PrintJob job);
/// print() with no owner window.
PrintResult print(PrintJob job);

/// Lays `content` out in `area` and draws it: a draw_page that prints controls or a built spec.
///
/// Example:
/// @code
/// ui::print(window, ui::PrintJob{
///     .title      = "Hello",
///     .page_count = [](const ui::PrintPage&, const ui::PrintContext&) { return std::size_t{1}; },
///     .draw_page  = [](ui::Renderer& renderer, const ui::PrintPage& page, const ui::PrintContext& context)
///     {
///         std::unique_ptr<ui::Element> sheet = ui::build(ui::Text{.text = "Hello", .role = ui::TextRole::Title});
///         ui::draw_element(renderer, *sheet, page.printable, context);
///     },
/// });
/// @endcode
void draw_element(Renderer& renderer, Element& content, const Rect& area, const PrintContext& context);

/// What print() hands the job to: the platform's print dialog and spooler, or a test double
/// (ui::testing::TestPrintHost).
class PrintHost
{
public:
    virtual ~PrintHost() = default;
    /// Runs `job` over `owner` (nullptr: none) and says how it ended.
    virtual PrintResult print(const Window* owner, PrintJob& job) = 0;
};

namespace detail
{
// The host print() uses in place of the platform's, or nullptr for the platform's own.
// ui::testing::set_print_host() is the public way to set it.
void                     install_print_host(PrintHost* host);
[[nodiscard]] PrintHost* installed_print_host();
} // namespace detail

} // namespace ui
